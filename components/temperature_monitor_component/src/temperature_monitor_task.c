#include "temperature_monitor_internal.h"
#include "temperature_monitor_component.h"
#include "utils.h"

#include "esp_event.h"
#include "logger_component.h"

static const char *TAG = "TEMP_MONITOR_TASK";

ESP_EVENT_DEFINE_BASE(TEMP_MONITOR_EVENT);

const TempMonitorConfig_t temp_monitor_config = {
    .task_name = "TEMP_MONITOR_TASK",
    .stack_size = 8192,
    .task_priority = 5};

static esp_err_t post_temp_monitor_error(temp_monitor_context_t *ctx, temp_monitor_error_t type, esp_err_t esp_err_code);

static esp_err_t post_temp_monitor_event(temp_monitor_context_t *ctx, temp_monitor_event_t event_type, void *event_data, size_t event_data_size);

static temp_monitor_error_t map_esp_err_to_temp_monitor_error(esp_err_t err);

// ----------------------------
// Task
// ----------------------------
static void temp_monitor_task(void *args)
{
    temp_monitor_context_t *ctx = (temp_monitor_context_t *)args;
    
    LOGGER_LOG_INFO(TAG, "Temperature monitor task started");
    TickType_t last_wake = xTaskGetTickCount();

    static const uint8_t max_bad_samples = (CONFIG_TEMP_SENSORS_MAXIMUM_BAD_SAMPLES_PER_BATCH_PERCENT * CONFIG_TEMP_SENSORS_SAMPLING_FREQ_HZ) / 100;
    static uint8_t samples_collected = 0;
    static uint8_t bad_samples_collected = 0;

    static const uint8_t samples_per_scond = CONFIG_TEMP_SENSORS_SAMPLING_FREQ_HZ;
    static const uint8_t delay_between_samples = 1000 / samples_per_scond;
    const TickType_t period = pdMS_TO_TICKS(delay_between_samples);

    while (ctx->monitor_running)
    {
        esp_err_t ret;

        // Read temperature sensors data
        for (uint8_t retry = 0; retry < CONFIG_TEMP_SENSOR_MAX_READ_RETRIES; retry++)
        {
            ret = read_temp_sensors_data(ctx, &ctx->current_sample);

            if (ret == ESP_OK && ctx->current_sample.valid)
            {
                break;
            }

            LOGGER_LOG_WARN(TAG, "Retrying to read temperature sensors data (%d/%d)", retry + 1, CONFIG_TEMP_SENSOR_MAX_READ_RETRIES);

            vTaskDelay(pdMS_TO_TICKS(CONFIG_TEMP_SENSOR_RETRY_DELAY_MS));
        }

        if (!ctx->current_sample.valid)
        {
            bad_samples_collected++;
        }

        if (ret != ESP_OK)
        {
            post_temp_monitor_error(ctx, map_esp_err_to_temp_monitor_error(ret), ret);
            LOGGER_LOG_ERROR(TAG, "Failed to get temperatures after retries: %s",
                             esp_err_to_name(ret));
            continue;
        }

        temp_ring_buffer_push(&ctx->ring_buffer, &ctx->current_sample);

        // Post buffer ready event after samples collected
        samples_collected++;
        if (samples_collected >= samples_per_scond)
        {
            if (bad_samples_collected >= max_bad_samples)
            {
                LOGGER_LOG_ERROR(TAG, "Too many bad samples collected (%d/%d)",
                                 bad_samples_collected,
                                 max_bad_samples);
                post_temp_monitor_event(ctx, TEMP_MONITOR_ERROR_OCCURRED, &ctx->current_sample, sizeof(ctx->current_sample));
            }
            else
            {
                LOGGER_LOG_INFO(TAG, "Samples collected: %d, Bad samples: %d",
                                samples_collected,
                                bad_samples_collected);
                xEventGroupSetBits(ctx->processor_event_group, TEMP_READY_EVENT_BIT);
            }

            samples_collected = 0;
            bad_samples_collected = 0;
        }

        vTaskDelayUntil(&last_wake, period);
    }

    LOGGER_LOG_INFO(TAG, "Temperature monitor task exiting");
    vTaskDelete(NULL);
}

esp_err_t start_temperature_monitor_task(temp_monitor_context_t *ctx)
{
    if (ctx->monitor_running)
    {
        return ESP_OK;
    }

    ctx->current_sample.number_of_attached_sensors = ctx->number_of_attached_sensors;

    ctx->monitor_running = true;

    bool init_temp_ring_buffer_result = temp_ring_buffer_init(&ctx->ring_buffer);
    if (!init_temp_ring_buffer_result)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to initialize temperature ring buffer");
        ctx->monitor_running = false;
        return ESP_FAIL;
    }

    CHECK_ERR_LOG_CALL_RET(xTaskCreate(
                               temp_monitor_task,
                               temp_monitor_config.task_name,
                               temp_monitor_config.stack_size,
                               ctx,
                               temp_monitor_config.task_priority,
                               &ctx->task_handle) == pdPASS
                               ? ESP_OK
                               : ESP_FAIL,
                           ctx->monitor_running = false,
                           "Failed to create temperature monitor task");

    return ESP_OK;
}

esp_err_t stop_temperature_monitor_task(temp_monitor_context_t *ctx)
{
    if (!ctx->monitor_running)
    {
        return ESP_OK;
    }

    ctx->monitor_running = false;

    if (ctx->task_handle)
    {
        vTaskDelete(ctx->task_handle);
        ctx->task_handle = NULL;
    }

    return ESP_OK;
}

// Post temperature monitor error event
static esp_err_t post_temp_monitor_error(temp_monitor_context_t *ctx, temp_monitor_error_t type, esp_err_t esp_err_code)
{
    temp_monitor_error_data_t error_data = {
        .type = type,
        .esp_err_code = esp_err_code};

    return post_temp_monitor_event(ctx, TEMP_MONITOR_ERROR_OCCURRED, &error_data, sizeof(error_data));
}

static esp_err_t post_temp_monitor_event(temp_monitor_context_t *ctx, temp_monitor_event_t event_type, void *event_data, size_t event_data_size)
{

    CHECK_ERR_LOG_RET(esp_event_post_to(
                          ctx->temperature_event_loop_handle,
                          TEMP_MONITOR_EVENT,
                          event_type,
                          event_data,
                          event_data_size,
                          portMAX_DELAY),
                      "Failed to post temperature monitor event");

    return ESP_OK;
}

// Map esp_err_t to temp_monitor_error_data_t
static temp_monitor_error_t map_esp_err_to_temp_monitor_error(esp_err_t err)
{
    switch (err)
    {
    case ESP_ERR_INVALID_ARG:
    case ESP_ERR_INVALID_STATE:
        return TEMP_MONITOR_ERR_SENSOR_READ;
    case ESP_ERR_TIMEOUT:
        return TEMP_MONITOR_ERR_SPI;
    default:
        return TEMP_MONITOR_ERR_UNKNOWN;
    }
}

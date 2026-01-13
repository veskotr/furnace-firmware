#include "temperature_monitor_internal.h"
#include "temperature_monitor_component.h"
#include "utils.h"
#include "event_registry.h"
#include "event_manager.h"
#include "logger_component.h"
#include "error_manager.h"

static const char* TAG = "TEMP_MONITOR_TASK";

const TempMonitorConfig_t temp_monitor_config = {
    .task_name = "TEMP_MONITOR_TASK",
    .stack_size = 8192,
    .task_priority = 5
};

static const uint8_t max_bad_samples = (CONFIG_TEMP_SENSORS_MAXIMUM_BAD_SAMPLES_PER_BATCH_PERCENT *
    CONFIG_TEMP_SENSORS_SAMPLING_FREQ_HZ) / 100;

static const uint8_t samples_per_second = CONFIG_TEMP_SENSORS_SAMPLING_FREQ_HZ;

// ----------------------------
// Event Helper Functions
// ----------------------------

/**
 * @brief Post a temperature monitor error event using the event manager
 */
static esp_err_t post_temperature_error(furnace_error_t error_type);

static esp_err_t read_sensors_with_retry(const temp_monitor_context_t* ctx, temp_sample_t* sample);

static void check_sensor_sample(temp_monitor_context_t* ctx, temp_sample_t* sample,
                                furnace_error_t* errors, uint8_t* num_errors);

static void process_sample(temp_monitor_context_t* ctx);

static void post_temp_monitor_error_summary(temp_monitor_context_t* ctx);

static furnace_error_t record_hw_error(temp_monitor_context_t* ctx, temp_monitor_hw_error_type_t hw_error_type,
                                       const temp_sensor_t* sensor);

static furnace_error_t record_data_error(temp_monitor_context_t* ctx, temp_monitor_data_error_type_t data_error_type,
                                         uint8_t data, uint8_t info);

static furnace_error_t record_timeout_error(temp_monitor_context_t* ctx, const esp_err_t esp_error);

static furnace_error_severity_t determine_hw_error_severity(temp_monitor_context_t* ctx);

uint32_t build_temp_monitor_error_code(const temp_monitor_context_t* ctx);

// ----------------------------
// Task
// ----------------------------
// ReSharper disable CppDFAUnreachableCode
static void temp_monitor_task(void* args)
{
    temp_monitor_context_t* ctx = (temp_monitor_context_t*)args;

    LOGGER_LOG_INFO(TAG, "Temperature monitor task started");
    TickType_t last_wake = xTaskGetTickCount();


    static const uint8_t delay_between_samples = 1000 / samples_per_second;
    const TickType_t period = pdMS_TO_TICKS(delay_between_samples);

    while (ctx->monitor_running)
    {
        const esp_err_t error = read_sensors_with_retry(ctx, &ctx->current_sample);

        if (error != ESP_OK)
        {
            post_temperature_error(record_timeout_error(ctx, error));
            LOGGER_LOG_ERROR(TAG, "Failed to get temperatures after retries: %s",
                             esp_err_to_name(error));
            ctx->current_sample.valid = false;
            // Handle critical error
        }

        check_sensor_sample(ctx, &ctx->current_sample, ctx->error_buffer, &ctx->num_errors);

        temp_ring_buffer_push(&ctx->ring_buffer, &ctx->current_sample);

        process_sample(ctx);

        post_temp_monitor_error_summary(ctx);

        event_manager_post_health(TEMP_MONITOR_EVENT_HEARTBEAT);
        const uint32_t ticks_to_wait = (last_wake + period) - xTaskGetTickCount();
        if (ticks_to_wait > 0)
            ulTaskNotifyTake(pdTRUE, ticks_to_wait);
        last_wake += period;
    }

    LOGGER_LOG_INFO(TAG, "Temperature monitor task exiting");
    ctx->task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t start_temperature_monitor_task(temp_monitor_context_t* ctx)
{
    if (ctx->monitor_running)
    {
        return ESP_OK;
    }

    ctx->current_sample.number_of_attached_sensors = ctx->number_of_attached_sensors;

    bool init_temp_ring_buffer_result = temp_ring_buffer_init(&ctx->ring_buffer);
    if (!init_temp_ring_buffer_result)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to initialize temperature ring buffer");
        ctx->monitor_running = false;
        return ESP_FAIL;
    }

    ctx->monitor_running = true;

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

esp_err_t stop_temperature_monitor_task(temp_monitor_context_t* ctx)
{
    if (!ctx->monitor_running)
    {
        return ESP_OK;
    }

    ctx->monitor_running = false;
    if (ctx->task_handle != NULL)
    {
        xTaskNotifyGive(ctx->task_handle);
    }

    return ESP_OK;
}

static esp_err_t post_temperature_error(furnace_error_t furnace_error)
{
    CHECK_ERR_LOG_RET(event_manager_post_blocking(FURNACE_ERROR_EVENT,
                          FURNACE_ERROR_EVENT_ID,
                          &furnace_error,
                          sizeof(furnace_error_t)),
                      "Failed to post temperature monitor error event");

    return ESP_OK;
}

static esp_err_t read_sensors_with_retry(const temp_monitor_context_t* ctx, temp_sample_t* sample)
{
    for (uint8_t retry = 0; retry < CONFIG_TEMP_SENSOR_MAX_READ_RETRIES; retry++)
    {
        read_temp_sensors_data(ctx, sample);
        if (sample->valid && !sample->empty)
        {
            return ESP_OK;
        }
        LOGGER_LOG_WARN(TAG, "Retrying to read temperature sensors data (%d/%d)", retry + 1,
                        CONFIG_TEMP_SENSOR_MAX_READ_RETRIES);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_TEMP_SENSOR_RETRY_DELAY_MS));
    }

    return ESP_ERR_TIMEOUT;
}

static void check_sensor_sample(temp_monitor_context_t* ctx, temp_sample_t* sample,
                                furnace_error_t* errors, uint8_t* num_errors)
{
    *num_errors = 0;
    for (uint8_t i = 0; i < ctx->number_of_attached_sensors; i++)
    {
        temp_sensor_t* sensor = &sample->sensors[i];
        if (!sensor->valid)
        {
            errors[(*num_errors)++] = record_hw_error(ctx, TEMP_MONITOR_ERROR_SENSOR_READ, sensor);
            continue;
        }
        if (sensor->temperature_c > CONFIG_TEMP_SENSOR_MAX_TEMPERATURE_C)
        {
            errors[(*num_errors)++] = record_data_error(ctx,
                                                        TEMP_MONITOR_ERROR_OVER_TEMP,
                                                        sensor->index,
                                                        0);
        }
    }
    if (!sample->valid)
    {
        ctx->bad_samples_collected++;
    }
}

static void process_sample(temp_monitor_context_t* ctx)
{
    if (!ctx->current_sample.valid || ctx->num_errors > 0)
    {
        ctx->bad_samples_collected++;
    }

    ctx->samples_collected++;

    if (ctx->samples_collected >= samples_per_second)
    {
        if (ctx->bad_samples_collected >= max_bad_samples)
        {
            LOGGER_LOG_ERROR(TAG,
                             "Too many bad samples collected (%d/%d)",
                             ctx->bad_samples_collected,
                             max_bad_samples);

            ctx->error_buffer[ctx->num_errors++] = record_data_error(ctx,
                                                                     TEMP_MONITOR_ERROR_TOO_MANY_SAMPLES,
                                                                     ctx->bad_samples_collected,
                                                                     max_bad_samples);
        }
        else
        {
            LOGGER_LOG_INFO(TAG,
                            "Samples collected: %d, Bad samples: %d",
                            ctx->samples_collected,
                            ctx->bad_samples_collected);

            xEventGroupSetBits(
                ctx->processor_event_group,
                TEMP_READY_EVENT_BIT
            );
        }

        ctx->samples_collected = 0;
        ctx->bad_samples_collected = 0;
    }
}

static void post_temp_monitor_error_summary(temp_monitor_context_t* ctx)
{
    if (ctx->num_errors > 0)
    {
        const furnace_error_t error = {
            .severity = ctx->highest_error_severity,
            .source = SOURCE_TEMP_MONITOR,
            .error_code = build_temp_monitor_error_code(ctx),
        };
        post_temperature_error(error);
        ctx->num_errors = 0;
        ctx->num_hw_errors = 0;
        ctx->num_data_errors = 0;
        ctx->num_over_temp_errors = 0;
        ctx->highest_error_severity = SEVERITY_INFO;
    }
}

static furnace_error_t record_data_error(temp_monitor_context_t* ctx,
                                         const temp_monitor_data_error_type_t data_error_type, const uint8_t data,
                                         const uint8_t info)
{
    if (data_error_type == TEMP_MONITOR_ERROR_TOO_MANY_SAMPLES)
        ctx->num_data_errors++;

    if (data_error_type == TEMP_MONITOR_ERROR_OVER_TEMP)
        ctx->num_over_temp_errors++;

    if (ctx->highest_error_severity < SEVERITY_WARNING)
        ctx->highest_error_severity = SEVERITY_WARNING;

    const furnace_error_t error = {
        .severity = SEVERITY_WARNING,
        .error_code = ERROR_CODE(
            TEMP_MONITOR_DATA_ERROR,
            data_error_type,
            data,
            info),
        .source = SOURCE_TEMP_MONITOR,
    };
    return error;
}


static furnace_error_t record_hw_error(temp_monitor_context_t* ctx, const temp_monitor_hw_error_type_t hw_error_type,
                                       const temp_sensor_t* sensor)
{
    uint32_t error_code = 0;

    if (sensor->raw_fault_byte != 0)
    {
        error_code = ERROR_CODE(
            TEMP_MONITOR_HW_ERROR,
            hw_error_type,
            sensor->index,
            sensor->raw_fault_byte);
    }
    else if (sensor->error != ESP_OK)
    {
        error_code = ERROR_CODE(
            TEMP_MONITOR_HW_ERROR,
            hw_error_type,
            sensor->index,
            (uint8_t)(sensor->error & 0xFF));
    }

    ctx->num_hw_errors++;

    return (furnace_error_t){
        .severity = determine_hw_error_severity(ctx),
        .error_code = error_code,
        .source = SOURCE_TEMP_MONITOR,
    };
}

static furnace_error_t record_timeout_error(temp_monitor_context_t* ctx, const esp_err_t esp_error)
{
    ctx->num_hw_errors++;
    return (furnace_error_t){
        .severity = SEVERITY_CRITICAL,
        .error_code = ERROR_CODE(
            TEMP_MONITOR_HW_ERROR,
            TEMP_MONITOR_ERROR_SENSOR_READ,
            (uint8_t)((esp_error << 8) & 0xFF),
            (uint8_t)(esp_error & 0xFF)),
        .source = SOURCE_TEMP_MONITOR,
    };
}

uint32_t build_temp_monitor_error_code(const temp_monitor_context_t* ctx)
{
    uint8_t flags = 0;
    if (ctx->num_over_temp_errors > 0)
    {
        flags |= TEMP_ERR_TYPE_OVER_TEMP;
    }
    else if (ctx->num_data_errors > 0)
    {
        flags |= TEMP_ERR_TYPE_DATA;
    }
    else if (ctx->num_hw_errors > 0)
    {
        flags |= TEMP_ERR_TYPE_HW;
    }
    return ERROR_CODE(
        flags,
        ctx->num_over_temp_errors,
        ctx->num_hw_errors,
        ctx->num_data_errors);
}

static furnace_error_severity_t determine_hw_error_severity(temp_monitor_context_t* ctx)
{
    if (ctx->num_hw_errors > CONFIG_TEMP_SENSORS_MAX_SENSOR_FAILURES)
    {
        ctx->highest_error_severity = SEVERITY_CRITICAL;
        return SEVERITY_CRITICAL;
    }
    if (ctx->highest_error_severity < SEVERITY_WARNING)
    {
        ctx->highest_error_severity = SEVERITY_WARNING;
    }
    return SEVERITY_WARNING;
}

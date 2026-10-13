#include <string.h>

#include "event_manager.h"
#include "temperature_processor_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger_component.h"
#include "utils.h"
#include "sdkconfig.h"
#include "furnace_error_types.h"

static const char* TAG = "TEMP_PROCESSOR_TASK";

// ----------------------------
// Configuration
// ----------------------------
typedef struct
{
    const char* task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} TempProcessorConfig_t;

static const TempProcessorConfig_t temp_processor_config = {
    .task_name = CONFIG_TEMP_PROCESSOR_TASK_NAME,
    .stack_size = CONFIG_TEMP_PROCESSOR_TASK_STACK_SIZE,
    .task_priority = CONFIG_TEMP_PROCESSOR_TASK_PRIORITY
};

static const health_monitor_data_t health_monitor_data = {
    .component_id = CONFIG_TEMP_PROCESSOR_COMPONENT_ID,
    .component_name = "Temperature Processor",
    .timeout_ticks = pdMS_TO_TICKS(CONFIG_TEMP_PROCESSOR_HEART_BEAT_TIMEOUT_MS)
};

static void read_temp_sensors(temp_processor_context_t* ctx, uint8_t* number_of_samples);

// ----------------------------
// Task
// ----------------------------
static void temp_process_task(void* args)
{
    LOGGER_LOG_INFO(TAG, "Temperature processor task started");

    temp_processor_context_t* ctx = (temp_processor_context_t*)args;

    while (atomic_load_explicit(&ctx->processor_running, memory_order_acquire))
    {
        uint8_t samples_count = 0;

        read_temp_sensors(ctx, &samples_count);

        float average_temperature = 0.0f;
        esp_err_t result = samples_count == 0
                               ? ESP_ERR_NOT_FOUND
                               : process_temperature_samples(ctx, samples_count, &average_temperature);

        const uint8_t fresh_count = samples_count;
        const TickType_t now = xTaskGetTickCount();
        const uint8_t minimum_valid = ctx->number_of_temp_sensors > 2
                                           ? (uint8_t)(ctx->number_of_temp_sensors - 2)
                                           : 1;
        temperature_processor_sample_t sample = {
            .average_temperature = average_temperature,
            .sample_tick = now,
            .valid_sensor_count = fresh_count,
            .total_sensor_count = ctx->number_of_temp_sensors,
            .valid = result == ESP_OK && fresh_count >= minimum_valid,
        };

        if (result != ESP_OK)
        {
            LOGGER_LOG_WARN(TAG, "Temperature processing encountered errors: type %d", esp_err_to_name(result));

            furnace_error_t furnace_error = {
                .source = SOURCE_TEMP_PROCESSOR,
                .severity = SEVERITY_WARNING,
                .error_code = result
            };
            CHECK_ERR_LOG(post_processing_error(furnace_error),
                          "Failed to post temp process error");
        }
        else
        {
            LOGGER_LOG_INFO(TAG, "Processed average temperature: %.2f C", average_temperature);
        }

        if (sample.valid)
        {
            CHECK_ERR_LOG(post_temp_processor_event(average_temperature),
                          "Failed to post temp process data");
        }
        CHECK_ERR_LOG(post_temp_processor_validity_event(&sample),
                      "Failed to post temp validity data");
        event_manager_post_health(HEALTH_MONITOR_EVENT_HEARTBEAT, &health_monitor_data);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    LOGGER_LOG_INFO(TAG, "Temperature processor task exiting");
    xSemaphoreGive(ctx->exit_semaphore);
    vTaskDelete(NULL);
}

esp_err_t start_temp_processor_task(temp_processor_context_t* ctx)
{
    if (ctx->task_handle)
    {
        return ESP_OK;
    }

    CHECK_ERR_LOG_CALL_RET(xTaskCreate(
                               temp_process_task,
                               temp_processor_config.task_name,
                               temp_processor_config.stack_size,
                               ctx,
                               temp_processor_config.task_priority,
                               &ctx->task_handle) == pdPASS
                           ? ESP_OK
                           : ESP_FAIL,
                           ctx->task_handle = NULL,
                           "Failed to create temperature processor task");

    event_manager_post_health(HEALTH_MONITOR_EVENT_REGISTER, &health_monitor_data);

    return ESP_OK;
}

esp_err_t stop_temp_processor_task(temp_processor_context_t* ctx)
{
    if (!ctx->task_handle)
    {
        return ESP_OK;
    }

    atomic_store_explicit(&ctx->processor_running, false, memory_order_release);
    if (ctx->task_handle != NULL)
    {
        xTaskNotifyGive(ctx->task_handle);
    }
    LOGGER_LOG_INFO(TAG, "Stopping temperature processor task");

    return ESP_OK;
}

static void read_temp_sensors(temp_processor_context_t* ctx, uint8_t* number_of_samples)
{
    const TickType_t now = xTaskGetTickCount();
    for (uint8_t i = 0; i < ctx->number_of_temp_sensors; i++)
    {
        const temp_sensor_device_t* temp_sensor_device = ctx->temp_sensor_devices[i];
        if (temp_sensor_device == NULL)
        {
            LOGGER_LOG_WARN(TAG, "Temperature processor sensor device at index %d is NULL", i);
            continue;
        }

        if (temp_sensor_read_device(temp_sensor_device, &ctx->temperatures_buffer[i])!= ESP_OK)
        {
            LOGGER_LOG_WARN(TAG, "Failed to read temperature from sensor device at index %d", i);
            continue;
        }

        if (!temp_sensor_has_successful_sample(temp_sensor_device) ||
            (now - temp_sensor_get_last_update_tick(temp_sensor_device)) >
                pdMS_TO_TICKS(CONFIG_COORDINATOR_SENSOR_AGGREGATE_STALE_TIMEOUT_MS))
        {
            LOGGER_LOG_WARN(TAG, "Ignoring stale temperature from sensor device at index %d", i);
            continue;
        }

        /* Compact successful samples so a failed earlier sensor cannot leave a
         * stale slot inside the aggregate. */
        if (*number_of_samples != i)
        {
            ctx->temperatures_buffer[*number_of_samples] = ctx->temperatures_buffer[i];
        }
        ++(*number_of_samples);
        LOGGER_LOG_DEBUG(TAG, "Read temperature %.2f C from sensor device at index %d", ctx->temperatures_buffer[*number_of_samples - 1], i);
    }
}

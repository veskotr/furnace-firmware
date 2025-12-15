#include "temperature_processor_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger_component.h"
#include "utils.h"
#include "temperature_monitor_component.h"
#include "sdkconfig.h"

static const char *TAG = "TEMP_PROCESSOR_TASK";

static temp_sample_t samples_buffer[CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE] = {0};

// ----------------------------
// Configuration
// ----------------------------
typedef struct
{
    const char *task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} TempProcessorConfig_t;

static const TempProcessorConfig_t temp_processor_config = {
    .task_name = "TEMP_CALC_TASK",
    .stack_size = 8192,
    .task_priority = 5};

// ----------------------------
// Task
// ----------------------------
static void temp_process_task(void *args)
{
    LOGGER_LOG_INFO(TAG, "Temperature processor task started");

    temp_processor_context_t *ctx = (temp_processor_context_t *)args;

    while (ctx->processor_running)
    {
        // Wait for temperature ready event
        EventGroupHandle_t event_group = temp_monitor_get_event_group();
        if (event_group == NULL)
        {
            LOGGER_LOG_ERROR(TAG, "Temperature monitor event group not available");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        xEventGroupWaitBits(event_group, TEMP_READY_EVENT_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

        // Process temperature samples from ring buffer
        size_t samples_count = temp_ring_buffer_pop_all(samples_buffer, CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE);

        if (samples_count == 0)
        {
            LOGGER_LOG_WARN(TAG, "No temperature samples available for processing");
            continue;
        }

        float average_temperature = 0.0f;
        process_temperature_error_t result = process_temperature_samples(ctx, samples_buffer, samples_count, &average_temperature);

        if (result.error_type != PROCESS_TEMPERATURE_ERROR_NONE)
        {
            LOGGER_LOG_WARN(TAG, "Temperature processing encountered errors: type %d", result.error_type);
            CHECK_ERR_LOG(post_temp_processor_event(PROCESS_TEMPERATURE_EVENT_ERROR, &result, sizeof(result)), "Failed to post temp process error");
        }
        else
        {
            LOGGER_LOG_INFO(TAG, "Processed average temperature: %.2f C", average_temperature);
        }
        CHECK_ERR_LOG(post_temp_processor_event(PROCESS_TEMPERATURE_EVENT_DATA, &average_temperature, sizeof(float)), "Failed to post temp process data");
    }

    LOGGER_LOG_INFO(TAG, "Temperature processor task exiting");
    vTaskDelete(NULL);
}

esp_err_t start_temp_processor_task(temp_processor_context_t *ctx)
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

    return ESP_OK;
}

esp_err_t stop_temp_processor_task(temp_processor_context_t *ctx)
{
    if (!ctx->task_handle)
    {
        return ESP_OK;
    }

    vTaskDelete(ctx->task_handle);
    ctx->task_handle = NULL;

    return ESP_OK;
}

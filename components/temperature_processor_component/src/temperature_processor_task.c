
#include "temperature_processor_task.h"
#include "esp_event.h"
#include "temperature_processor_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger_component.h"
#include "utils.h"
#include "temperature_monitor_component.h"
#include "temperature_processor.h"
#include "sdkconfig.h"

static const char *TAG = TEMP_PROCESSOR_LOG_TAG;

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
    .stack_size = 4096,
    .task_priority = 5};

// Task handle
static TaskHandle_t temp_processor_task_handle = NULL;

// ----------------------------
// Task
// ----------------------------
static void temp_process_task(void *args)
{
    LOGGER_LOG_INFO(TAG, "Temperature processor task started");

    while (processor_running)
    {
        // Wait for temperature ready event
        xEventGroupWaitBits(temp_processor_event_group, TEMP_READY_EVENT_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

        // Process temperature samples from ring buffer
        size_t samples_count = temp_ring_buffer_pop_all(samples_buffer, CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE);

        if (samples_count == 0)
        {
            LOGGER_LOG_WARN(TAG, "No temperature samples available for processing");
            continue;
        }

        float average_temperature = 0.0f;
        process_temperature_error_t result = process_temperature_samples(samples_buffer, samples_count, &average_temperature);

        if (result.error_type != PROCESS_TEMPERATURE_ERROR_NONE)
        {
            LOGGER_LOG_WARN(TAG, "Temperature processing encountered errors: type %d", result.error_type);
        }
        else
        {
            LOGGER_LOG_INFO(TAG, "Processed average temperature: %.2f C", average_temperature);
        }
    }

    LOGGER_LOG_INFO(TAG, "Temperature processor task exiting");
    vTaskDelete(NULL);
}

esp_err_t start_temp_processor_task(void)
{
    if (temp_processor_task_handle)
    {
        return ESP_OK;
    }

    CHECK_ERR_LOG_CALL_RET(xTaskCreate(
                               temp_process_task,
                               temp_processor_config.task_name,
                               temp_processor_config.stack_size,
                               NULL,
                               temp_processor_config.task_priority,
                               &temp_processor_task_handle) == pdPASS
                               ? ESP_OK
                               : ESP_FAIL,
                           temp_processor_task_handle = NULL,
                           "Failed to create temperature processor task");

    return ESP_OK;
}

esp_err_t stop_temp_processor_task(void)
{
    if (!temp_processor_task_handle)
    {
        return ESP_OK;
    }

    vTaskDelete(temp_processor_task_handle);
    temp_processor_task_handle = NULL;

    return ESP_OK;
}
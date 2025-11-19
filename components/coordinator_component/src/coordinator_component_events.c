#include "coordinator_component_events.h"
#include "utils.h"
#include "coordinator_component_log.h"
#include "temperature_monitor_component.h"
#include "temperature_processor_component.h"
#include "temperature_profile_controller.h"
#include "coordinator_component_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger_component.h"

const char *TAG = COORDINATOR_COMPONENT_LOG;

esp_event_loop_handle_t coordinator_event_loop_handle = NULL;

float coordinator_current_temperature = 0.0f;

static void handle_temperature_error(temp_monitor_error_data_t *error_data);

void temperature_event_handler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id)
    {
    case TEMP_MONITOR_ERROR_OCCURRED:
    {
        temp_monitor_error_data_t *error_data = (temp_monitor_error_data_t *)event_data;
        handle_temperature_error(error_data);
    }
    break;
    default:
        LOGGER_LOG_WARN(TAG, "Unknown Event ID: %d", id);
        break;
    }
}

void temperature_processor_event_handler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id)
    {
    case PROCESS_TEMPERATURE_EVENT_DATA:
    {
        coordinator_current_temperature = *((float *)event_data);
        LOGGER_LOG_INFO(TAG, "Average Temperature Processed: %.2f C", coordinator_current_temperature);
        if (coordinator_running)
        {
            xTaskNotifyGive(coordinator_task_handle);
        }
    }
    break;
    case PROCESS_TEMPERATURE_EVENT_ERROR:
    {
        // TODO: Handle temperature processing error
        process_temperature_error_t *result = (process_temperature_error_t *)event_data;

        LOGGER_LOG_ERROR(TAG, "Temperature Processing Error. Type: %d, Sensor Index: %d",
                         result->error_type,
                         result->sensor_index);
    }
    break;

    default:
        break;
    }
}

esp_err_t init_coordinator_events(void)
{
    esp_event_loop_args_t loop_args = {
        .queue_size = 10,
        .task_name = "temp_event_loop"};

    CHECK_ERR_LOG_RET(esp_event_loop_create(&loop_args, (esp_event_loop_handle_t *)&coordinator_event_loop_handle),
                      "Failed to create coordinator event loop");

    CHECK_ERR_LOG_RET(esp_event_handler_instance_register_with(coordinator_event_loop_handle,
                                                               TEMP_MONITOR_EVENT,
                                                               ESP_EVENT_ANY_ID,
                                                               &temperature_event_handler,
                                                               NULL,
                                                               NULL),
                      "Failed to register temperature event handler with coordinator event loop");

    CHECK_ERR_LOG_RET(esp_event_handler_instance_register_with(coordinator_event_loop_handle,
                                                               PROCESS_TEMPERATURE_EVENT,
                                                               ESP_EVENT_ANY_ID,
                                                               &temperature_processor_event_handler,
                                                               NULL,
                                                               NULL),
                      "Failed to register temperature processor event handler with coordinator event loop");

    return ESP_OK;
}

esp_err_t shutdown_coordinator_events(void)
{
    CHECK_ERR_LOG_RET(esp_event_loop_delete(coordinator_event_loop_handle),
                      "Failed to delete coordinator event loop");
    coordinator_event_loop_handle = NULL;

    return ESP_OK;
}

static void handle_temperature_error(temp_monitor_error_data_t *error_data)
{
    // Implement error handling logic here
    LOGGER_LOG_ERROR(TAG, "Handling temperature error. Type: %d, ESP Error Code: %d",
                     error_data->type,
                     error_data->esp_err_code);
    switch (error_data->type)
    {
    case TEMP_MONITOR_ERR_SENSOR_READ:
        // Handle sensor read error
        break;
    case TEMP_MONITOR_ERR_SENSOR_FAULT:
        // Handle sensor fault error
        break;
    case TEMP_MONITOR_ERR_SPI:
        // Handle SPI error
        break;
    case TEMP_MONITOR_ERR_TOO_MANY_BAD_SAMPLES:
        // Handle too many bad samples error
        break;
    case TEMP_MONITOR_ERR_UNKNOWN:
    default:
        // Handle unknown error
        break;
    }
}

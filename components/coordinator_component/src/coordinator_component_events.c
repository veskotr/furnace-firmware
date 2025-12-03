#include "coordinator_component_events.h"
#include "utils.h"
#include "coordinator_component_log.h"
#include "temperature_monitor_component.h"
#include "temperature_processor_component.h"
#include "temperature_profile_controller.h"
#include "heating_profile_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger_component.h"
#include "coordinator_component_types.h"

const char *TAG = COORDINATOR_COMPONENT_LOG;

ESP_EVENT_DEFINE_BASE(COORDINATOR_RX_EVENT);
ESP_EVENT_DEFINE_BASE(COORDINATOR_TX_EVENT);

esp_event_loop_handle_t coordinator_event_loop_handle = NULL;

float coordinator_current_temperature = 0.0f;

static void handle_temperature_error(temp_monitor_error_data_t *error_data);

static void temperature_event_handler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data);

static void temperature_processor_event_handler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data);

static void coordinator_event_handler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data);

static void temperature_event_handler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data)
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

static void temperature_processor_event_handler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data)
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

static void handle_temperature_error(temp_monitor_error_data_t *error_data)
{
    // Implement error handling logic here
    LOGGER_LOG_ERROR(TAG, "Handling temperature error. Type: %d, ESP Error Code: %d",
                     error_data->type,
                     error_data->esp_err_code);
    switch (error_data->type)
    {
    case TEMP_MONITOR_ERR_SENSOR_READ:
        // TODO Handle sensor read error
        break;
    case TEMP_MONITOR_ERR_SENSOR_FAULT:
        // TODO Handle sensor fault error
        break;
    case TEMP_MONITOR_ERR_SPI:
        // TODO Handle SPI error
        break;
    case TEMP_MONITOR_ERR_TOO_MANY_BAD_SAMPLES:
        // TODO Handle too many bad samples error
        break;
    case TEMP_MONITOR_ERR_UNKNOWN:
    default:
        // TODO Handle unknown error
        break;
    }
}

static void coordinator_event_handler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id)
    {
    case COORDINATOR_EVENT_START_PROFILE:
    {
        size_t profile_index = *((size_t *)event_data);
        LOGGER_LOG_INFO(TAG, "Coordinator Event: Start Profile Index %zu", profile_index);
        esp_err_t err = start_heating_profile((int)profile_index);
        if (err != ESP_OK)
        {
            CHECK_ERR_LOG_RET(send_coordinator_error_event(COORDINATOR_EVENT_ERROR_OCCURRED, &err, COORDINATOR_ERROR_NOT_STARTED),
                              "Failed to send coordinator error event for start profile failure");
            LOGGER_LOG_ERROR(TAG, "Failed to start heating profile index %zu: %s",
                             profile_index,
                             esp_err_to_name(err));
            return;
        }
        break;
    }
    case COORDINATOR_EVENT_PAUSE_PROFILE:
    {
        LOGGER_LOG_INFO(TAG, "Coordinator Event: Pause Profile");
        esp_err_t err = pause_heating_profile();
        if (err != ESP_OK)
        {
            CHECK_ERR_LOG_RET(send_coordinator_error_event(COORDINATOR_EVENT_ERROR_OCCURRED, &err, COORDINATOR_ERROR_PROFILE_NOT_PAUSED),
                              "Failed to send coordinator error event for pause profile failure");
            LOGGER_LOG_ERROR(TAG, "Failed to pause heating profile: %s",
                             esp_err_to_name(err));
            return;
        }
        break;
    }
    case COORDINATOR_EVENT_STOP_PROFILE:
    {
        LOGGER_LOG_INFO(TAG, "Coordinator Event: Stop Profile");
        esp_err_t err = stop_heating_profile();
        if (err != ESP_OK)
        {
            CHECK_ERR_LOG_RET(send_coordinator_error_event(COORDINATOR_EVENT_ERROR_OCCURRED, &err, COORDINATOR_ERROR_PROFILE_NOT_STOPPED),
                              "Failed to send coordinator error event for stop profile failure");
            LOGGER_LOG_ERROR(TAG, "Failed to stop heating profile: %s",
                             esp_err_to_name(err));
            return;
        }
        break;
    }
    case COORDINATOR_EVENT_RESUME_PROFILE:
    {
        LOGGER_LOG_INFO(TAG, "Coordinator Event: Resume Profile");
        esp_err_t err = resume_heating_profile();
        if (err != ESP_OK)
        {
            CHECK_ERR_LOG_RET(send_coordinator_error_event(COORDINATOR_EVENT_ERROR_OCCURRED, &err, COORDINATOR_ERROR_PROFILE_NOT_RESUMED),
                              "Failed to send coordinator error event for resume profile failure");
            LOGGER_LOG_ERROR(TAG, "Failed to resume heating profile: %s",
                             esp_err_to_name(err));
            return;
        }
        break;
    }
    case COORDINATOR_EVENT_GET_STATUS_REPORT:
    {
        LOGGER_LOG_INFO(TAG, "Coordinator Event: Get Status Report");
        heating_task_state_t state;
        get_heating_task_state(&state);
        CHECK_ERR_LOG_RET(send_coordinator_event(COORDINATOR_EVENT_GET_STATUS_REPORT, &state, sizeof(heating_task_state_t)),
                          "Failed to send coordinator status report event");
        break;
    }
    case COORDINATOR_EVENT_GET_CURRENT_PROFILE:
    {
        LOGGER_LOG_INFO(TAG, "Coordinator Event: Get Current Profile");
        size_t profile_index;
        get_current_heating_profile(&profile_index);
        CHECK_ERR_LOG_RET(send_coordinator_event(COORDINATOR_EVENT_GET_CURRENT_PROFILE, &profile_index, sizeof(size_t)),
                          "Failed to send coordinator current profile event");
        break;
    }
    default:
        LOGGER_LOG_WARN(TAG, "Unknown Coordinator Event ID: %d", id);
        break;
    }
}

esp_err_t send_coordinator_error_event(coordinator_tx_event_t event_type, esp_err_t *esp_error, coordinator_error_code_t coordinator_error_code)
{
    coordinator_error_t error = {
        .esp_error_code = *esp_error,
        .coordinator_error_code = coordinator_error_code};

    return send_coordinator_event(event_type, &error, sizeof(error));
}

esp_err_t send_coordinator_event(coordinator_tx_event_t event_type, void *event_data, size_t event_data_size)
{
    CHECK_ERR_LOG_RET_FMT(esp_event_post_to(coordinator_event_loop_handle,
                                            COORDINATOR_TX_EVENT,
                                            event_type,
                                            event_data,
                                            event_data_size,
                                            portMAX_DELAY),
                          "Failed to post coordinator event type %d",
                          event_type);
    return ESP_OK;
}

esp_err_t init_coordinator_events(void)
{
    esp_event_loop_args_t loop_args = {
        .queue_size = 10,
        .task_name = "coordinator_event_loop"};

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

    CHECK_ERR_LOG_RET(esp_event_handler_instance_register_with(coordinator_event_loop_handle,
                                                               COORDINATOR_RX_EVENT,
                                                               ESP_EVENT_ANY_ID,
                                                               &coordinator_event_handler,
                                                               NULL,
                                                               NULL),
                      "Failed to register coordinator event handler with coordinator event loop");

    return ESP_OK;
}

esp_err_t shutdown_coordinator_events(void)
{
    CHECK_ERR_LOG_RET(esp_event_loop_delete(coordinator_event_loop_handle),
                      "Failed to delete coordinator event loop");
    coordinator_event_loop_handle = NULL;

    return ESP_OK;
}

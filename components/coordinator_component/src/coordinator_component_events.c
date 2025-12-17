#include "utils.h"
#include "temperature_profile_controller.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger_component.h"
#include "coordinator_component_types.h"
#include "coordinator_component_internal.h"
#include "event_manager.h"
#include "event_registry.h"

static const char* TAG = "COORDINATOR_EVENTS";

float coordinator_current_temperature = 0.0f;

static void handle_temperature_error(const temp_monitor_error_event_t* error_data);

static void temperature_event_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data);

static void temperature_processor_event_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data);

static void coordinator_event_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data);

static void temperature_event_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    switch (id)
    {
    case TEMP_MONITOR_EVENT_ERROR_OCCURRED:
        {
            temp_monitor_error_event_t* error_data = (temp_monitor_error_event_t*)event_data;
            handle_temperature_error(error_data);
        }
        break;
    default:
        LOGGER_LOG_WARN(TAG, "Unknown Event ID: %d", id);
        break;
    }
}

static void temperature_processor_event_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    const coordinator_ctx_t* ctx = (coordinator_ctx_t*)handler_arg;

    switch (id)
    {
    case PROCESS_TEMPERATURE_EVENT_DATA:
        {
            coordinator_current_temperature = *((float*)event_data);
            LOGGER_LOG_INFO(TAG, "Average Temperature Processed: %.2f C", coordinator_current_temperature);
            if (ctx != NULL && ctx->running)
            {
                xTaskNotifyGive(ctx->task_handle);
            }
        }
        break;
    case PROCESS_TEMPERATURE_EVENT_ERROR:
        {
            // TODO: Handle temperature processing error
            const process_temperature_error_t* result = (process_temperature_error_t*)event_data;

            LOGGER_LOG_ERROR(TAG, "Temperature Processing Error. Type: %d, Sensor Index: %d",
                             result->error_type,
                             result->sensor_index);
        }
        break;

    default:
        // TODO Handle unknown event
        LOGGER_LOG_WARN(TAG, "Unknown Temperature Processor Event ID: %d", id);
        break;
    }
}

static void handle_temperature_error(const temp_monitor_error_event_t* error_data)
{
    // Implement error handling logic here
    LOGGER_LOG_ERROR(TAG, "Handling temperature error. Type: %d, ESP Error Code: %d",
                     error_data->error_code,
                     error_data->esp_error_code);
    switch (error_data->error_code)
    {
    case TEMP_MONITOR_ERROR_SENSOR_READ:
        // TODO Handle sensor read error
        break;
    case TEMP_MONITOR_ERROR_SENSOR_FAULT:
        // TODO Handle sensor fault error
        break;
    case TEMP_MONITOR_ERROR_SPI_FAULT:
        // TODO Handle SPI error
        break;
    case TEMP_MONITOR_ERROR_TOO_MANY_BAD_SAMPLES:
        // TODO Handle too many bad samples error
        break;
    case TEMP_MONITOR_ERROR_UNKNOWN:
    default:
        // TODO Handle unknown error
        break;
    }
}

static void coordinator_event_handler(void* handler_arg, esp_event_base_t base, const int32_t id, void* event_data)
{
    coordinator_ctx_t* ctx = (coordinator_ctx_t*)handler_arg;
    switch (id)
    {
    case COORDINATOR_EVENT_START_PROFILE:
        {
            const size_t profile_index = *((size_t*)event_data);
            LOGGER_LOG_INFO(TAG, "Coordinator Event: Start Profile Index %zu", profile_index);
            const esp_err_t err = start_heating_profile(ctx, profile_index);
            if (err != ESP_OK)
            {
                CHECK_ERR_LOG(
                    post_coordinator_error_event(COORDINATOR_EVENT_ERROR_OCCURRED, &err, COORDINATOR_ERROR_NOT_STARTED),
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
            const esp_err_t err = pause_heating_profile(ctx);
            if (err != ESP_OK)
            {
                CHECK_ERR_LOG(
                    post_coordinator_error_event(COORDINATOR_EVENT_ERROR_OCCURRED, &err,
                        COORDINATOR_ERROR_PROFILE_NOT_PAUSED),
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
            const esp_err_t err = stop_heating_profile(ctx);
            if (err != ESP_OK)
            {
                CHECK_ERR_LOG(
                    post_coordinator_error_event(COORDINATOR_EVENT_ERROR_OCCURRED, &err,
                        COORDINATOR_ERROR_PROFILE_NOT_STOPPED),
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
            esp_err_t err = resume_heating_profile(ctx);
            if (err != ESP_OK)
            {
                CHECK_ERR_LOG(
                    post_coordinator_error_event(COORDINATOR_EVENT_ERROR_OCCURRED, &err,
                        COORDINATOR_ERROR_PROFILE_NOT_RESUMED),
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
            get_heating_task_state(ctx, &state);
            CHECK_ERR_LOG(
                post_coordinator_event(COORDINATOR_EVENT_GET_STATUS_REPORT, &state, sizeof(heating_task_state_t)),
                "Failed to send coordinator status report event");
            break;
        }
    case COORDINATOR_EVENT_GET_CURRENT_PROFILE:
        {
            LOGGER_LOG_INFO(TAG, "Coordinator Event: Get Current Profile");
            size_t profile_index;
            get_current_heating_profile(ctx, &profile_index);
            CHECK_ERR_LOG(post_coordinator_event(COORDINATOR_EVENT_GET_CURRENT_PROFILE, &profile_index, sizeof(size_t)),
                          "Failed to send coordinator current profile event");
            break;
        }
    default:
        LOGGER_LOG_WARN(TAG, "Unknown Coordinator Event ID: %d", id);
        break;
    }
}

esp_err_t post_heater_controller_event(const heater_controller_event_t event_type, void* event_data,
                                       const size_t event_data_size)
{
    CHECK_ERR_LOG_RET_FMT(event_manager_post_blocking(
                              HEATER_CONTROLLER_EVENT,
                              event_type,
                              event_data,
                              event_data_size),
                          "Failed to post heater controller event type %d",
                          event_type);
    return ESP_OK;
}

esp_err_t post_coordinator_error_event(const coordinator_event_id_t event_type, const esp_err_t* esp_error,
                                       const coordinator_error_code_t coordinator_error_code)
{
    coordinator_error_data_t error = {
        .esp_error_code = *esp_error,
        .error_code = coordinator_error_code
    };

    return post_coordinator_event(event_type, &error, sizeof(error));
}

esp_err_t post_coordinator_event(const coordinator_event_id_t event_type, void* event_data,
                                 const size_t event_data_size)
{
    CHECK_ERR_LOG_RET_FMT(event_manager_post_blocking(
                              COORDINATOR_EVENT,
                              event_type,
                              event_data,
                              event_data_size),
                          "Failed to post coordinator event type %d",
                          event_type);
    return ESP_OK;
}

esp_err_t init_coordinator_events(coordinator_ctx_t* ctx)
{
    CHECK_ERR_LOG_RET(event_manager_subscribe(
                          TEMP_MONITOR_EVENT,
                          ESP_EVENT_ANY_ID,
                          &temperature_event_handler,
                          ctx),
                      "Failed to subscribe to temperature monitor events");

    CHECK_ERR_LOG_RET(event_manager_subscribe(
                          TEMP_PROCESSOR_EVENT,
                          ESP_EVENT_ANY_ID,
                          &temperature_processor_event_handler,
                          ctx),
                      "Failed to subscribe to temperature processor events");

    CHECK_ERR_LOG_RET(event_manager_subscribe(
                          COORDINATOR_EVENT,
                          ESP_EVENT_ANY_ID,
                          &coordinator_event_handler,
                          ctx),
                      "Failed to subscribe to coordinator events");

    ctx->events_initialized = true;

    return ESP_OK;
}

esp_err_t shutdown_coordinator_events(coordinator_ctx_t* ctx)
{
    if (!ctx->events_initialized)
    {
        return ESP_OK;
    }
    CHECK_ERR_LOG_RET(event_manager_unsubscribe(
                          COORDINATOR_EVENT,
                          ESP_EVENT_ANY_ID,
                          &coordinator_event_handler),
                      "Failed to unsubscribe from coordinator events");
    CHECK_ERR_LOG_RET(event_manager_unsubscribe(
                          TEMP_PROCESSOR_EVENT,
                          ESP_EVENT_ANY_ID,
                          &temperature_processor_event_handler),
                      "Failed to unsubscribe from temperature processor events");

    CHECK_ERR_LOG_RET(event_manager_unsubscribe(
                          TEMP_MONITOR_EVENT,
                          ESP_EVENT_ANY_ID,
                          &temperature_event_handler),
                      "Failed to unsubscribe from temperature monitor events");

    ctx->events_initialized = false;
    return ESP_OK;
}

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

static void coordinator_event_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data);

static void temperature_processor_event_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    coordinator_ctx_t* ctx = (coordinator_ctx_t*)handler_arg;

    if (id != PROCESS_TEMPERATURE_EVENT_DATA)
    {
        LOGGER_LOG_WARN(TAG, "Unknown Temperature Processor Event ID: %d", id);
        return;
    }
    else
    {
        if (event_data == NULL)
        {
            LOGGER_LOG_WARN(TAG, "Temperature Processor Event Data is NULL");
            return;
        }

        const temp_processor_data_t* data = (temp_processor_data_t*)event_data;
        if (!data->valid)
        {
            LOGGER_LOG_WARN(TAG, "Temperature processor data marked invalid");
            return;
        }
        ctx->current_temperature = data->average_temperature;
        ctx->heating_task_state.current_temperature = data->average_temperature;
        LOGGER_LOG_DEBUG(TAG, "Updated current temperature to %.2f C", ctx->current_temperature);
    }
}

static void coordinator_event_handler(void* handler_arg, esp_event_base_t base, const int32_t id, void* event_data)
{
    coordinator_ctx_t* ctx = (coordinator_ctx_t*)handler_arg;
    switch (id)
    {
    case COORDINATOR_EVENT_START_PROFILE:
        {
            if (event_data == NULL)
            {
                LOGGER_LOG_WARN(TAG, "Start profile event data is NULL");
                return;
            }
            const coordinator_start_profile_data_t* data = (coordinator_start_profile_data_t*)event_data;
            const size_t profile_index = data->profile_index;
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
            coordinator_start_profile_data_t started_data = { .profile_index = profile_index };
            post_coordinator_event(COORDINATOR_EVENT_PROFILE_STARTED, &started_data, sizeof(started_data));
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
            post_coordinator_event(COORDINATOR_EVENT_PROFILE_PAUSED, NULL, 0);
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
            post_coordinator_event(COORDINATOR_EVENT_PROFILE_STOPPED, NULL, 0);
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
            post_coordinator_event(COORDINATOR_EVENT_PROFILE_RESUMED, NULL, 0);
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
                          COORDINATOR_EVENT,
                          ESP_EVENT_ANY_ID,
                          &coordinator_event_handler,
                          ctx),
                      "Failed to subscribe to coordinator events");

    CHECK_ERR_LOG_RET(event_manager_subscribe(
                          TEMP_PROCESSOR_EVENT,
                          ESP_EVENT_ANY_ID,
                          &temperature_processor_event_handler,
                          ctx),
                      "Failed to subscribe to temperature processor events");

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


    ctx->events_initialized = false;
    return ESP_OK;
}

#include "commands_dispatcher.h"
#include "utils.h"
#include "temperature_profile_controller.h"
#include "freertos/FreeRTOS.h"
#include "logger_component.h"
#include "coordinator_component_types.h"
#include "coordinator_component_internal.h"
#include "event_manager.h"
#include "event_registry.h"

static const char* TAG = "COORDINATOR_EVENTS";

float coordinator_current_temperature = 0.0f;

static void temperature_processor_event_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    const coordinator_ctx_t* ctx = (coordinator_ctx_t*)handler_arg;

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
        //TODO Convert to correct type
        const float* temperature = (float*)event_data;
        coordinator_current_temperature = *temperature;
        LOGGER_LOG_DEBUG(TAG, "Updated current temperature to %.2f C", coordinator_current_temperature);
    }
}

static esp_err_t coordinator_command_handler(void* handler_arg, void* command_data, const size_t command_data_size)
{
    coordinator_ctx_t* ctx = (coordinator_ctx_t*)handler_arg;
    const coordinator_command_data_t* data = (coordinator_command_data_t*)command_data;
    if (data == NULL || command_data_size != sizeof(coordinator_command_data_t))
    {
        LOGGER_LOG_ERROR(TAG, "Invalid coordinator command data");
        return ESP_ERR_INVALID_ARG;
    }

    //TODO fix Logic for commands
    switch (data->type)
    {
    case COMMAND_TYPE_COORDINATOR_GET_CURRENT_PROFILE:
        return get_current_heating_profile(ctx);
    case COMMAND_TYPE_COORDINATOR_GET_STATUS_REPORT:
        return get_heating_task_state(ctx);
    case COMMAND_TYPE_COORDINATOR_PAUSE_PROFILE:
        return pause_heating_profile(ctx);
    case COMMAND_TYPE_COORDINATOR_RESUME_PROFILE:
        return resume_heating_profile(ctx);
    case COMMAND_TYPE_COORDINATOR_STOP_PROFILE:
        return stop_heating_profile(ctx);
    case COMMAND_TYPE_COORDINATOR_START_PROFILE:
        return start_heating_profile(ctx, data->profile_index);
    default:
        LOGGER_LOG_ERROR(TAG, "Unknown coordinator command type: %d", data->type);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
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
    CHECK_ERR_LOG_RET(register_command_handler(
                          COMMAND_TARGET_COORDINATOR,
                          &coordinator_command_handler,
                          ctx),
                      "Failed to register coordinator command handler");

    CHECK_ERR_LOG_RET(event_manager_subscribe(
                          TEMP_PROCESSOR_EVENT,
                          PROCESS_TEMPERATURE_EVENT_DATA,
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
    CHECK_ERR_LOG_RET(unregister_command_handler(COMMAND_TARGET_COORDINATOR),
                      "Failed to unsubscribe from coordinator events");


    ctx->events_initialized = false;
    return ESP_OK;
}

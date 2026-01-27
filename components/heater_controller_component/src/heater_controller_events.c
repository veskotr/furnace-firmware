#include "commands_dispatcher.h"
#include "heater_controller_internal.h"
#include "utils.h"
#include "event_manager.h"
#include "furnace_error_types.h"

static const char* TAG = "HEATER_CTRL_EVENTS";

static esp_err_t heater_command_handler(void* handler_arg, void* command_data, const size_t command_data_size);

esp_err_t init_events(heater_controller_context_t* ctx)
{
    CHECK_ERR_LOG_RET(register_command_handler(
                          COMMAND_TARGET_HEATER,
                          &heater_command_handler,
                          ctx),
                      "Failed to register heater controller command handler");
    return ESP_OK;
}

inline esp_err_t post_heater_controller_error(furnace_error_t error)
{
    return event_manager_post(FURNACE_ERROR_EVENT,
                              FURNACE_ERROR_EVENT_ID,
                              &error,
                              sizeof(furnace_error_t),
                              portMAX_DELAY);
}

esp_err_t post_heater_controller_event(heater_controller_event_t event_type, void* event_data,
                                       const size_t event_data_size)
{
    CHECK_ERR_LOG_RET(event_manager_post( HEATER_CONTROLLER_EVENT,
                          event_type,
                          event_data,
                          event_data_size,
                          portMAX_DELAY),
                      "Failed to post heater controller event");

    return ESP_OK;
}

static esp_err_t heater_command_handler(void* handler_arg, void* command_data, const size_t command_data_size)
{
    heater_controller_context_t* ctx = (heater_controller_context_t*)handler_arg;
    const heater_command_data_t* data = (heater_command_data_t*)command_data;
    if (data == NULL || command_data_size != sizeof(heater_command_data_t))
    {
        LOGGER_LOG_ERROR(TAG, "Invalid heater command data");
        return ESP_ERR_INVALID_ARG;
    }

    switch (data->type)
    {
    case COMMAND_TYPE_HEATER_SET_POWER:
        return set_heater_target_power_level(ctx, data->power_level);
    case COMMAND_TYPE_HEATER_GET_STATUS:
        return ESP_OK; //TODO Implement get status
    case COMMAND_TYPE_HEATER_TOGGLE:
        return toggle_heater(data->heater_state);
    default:
        LOGGER_LOG_ERROR(TAG, "Unknown heater command type: %d", data->type);
        return ESP_OK;
    }
}

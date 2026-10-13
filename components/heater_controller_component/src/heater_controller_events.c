#include "commands_dispatcher.h"
#include "heater_controller_internal.h"
#include "utils.h"
#include "event_manager.h"
#include "furnace_error_types.h"

static const char* TAG = "HEATER_CTRL_EVENTS";

static esp_err_t heater_command_handler(void* handler_arg, const void* command_data);

esp_err_t init_events(heater_controller_context_t* ctx)
{
    CHECK_ERR_LOG_RET(register_command_handler(
                          COMMAND_TARGET_HEATER,
                          &heater_command_handler,
                          ctx),
                      "Failed to register heater controller command handler");
    return ESP_OK;
}

esp_err_t post_heater_controller_error(furnace_error_t error)
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

static esp_err_t heater_command_handler(void* handler_arg, const void* command_data)
{
    heater_controller_context_t* ctx = (heater_controller_context_t*)handler_arg;
    if (command_data == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid heater command data");
        return ESP_ERR_INVALID_ARG;
    }
    const heater_command_data_t* data = (const heater_command_data_t*)command_data;

    switch (data->type)
    {
    case COMMAND_TYPE_HEATER_SET_POWER:
        return set_heater_target_power_level(ctx, data->power_level);
    case COMMAND_TYPE_HEATER_GET_STATUS:
        return ESP_OK; //TODO Implement get status
    case COMMAND_TYPE_HEATER_TOGGLE:
        if (data->heater_state && heater_output_is_inhibited(ctx))
        {
            LOGGER_LOG_ERROR(TAG, "Rejected heater-on toggle while output is inhibited");
            return ESP_ERR_INVALID_STATE;
        }
        return toggle_heater(data->heater_state);
    case COMMAND_TYPE_HEATER_CLEAR:
        /* Drop the SSR pin immediately — the heater task may be mid-cycle
         * with the SSR held high; without this it would finish its current
         * on-window before noticing the new zero power level. */
        {
            const esp_err_t err = toggle_heater(HEATER_OFF);
            if (err != ESP_OK)
            {
                heater_controller_handle_ssr_failure(ctx, err);
                return err;
            }
        }
        return clear_heater_target_power_level(ctx);
    case COMMAND_TYPE_HEATER_START:
        if (heater_output_is_inhibited(ctx))
        {
            LOGGER_LOG_ERROR(TAG, "Rejected heater start while output is inhibited");
            return ESP_ERR_INVALID_STATE;
        }
        return start_heater();
    case COMMAND_TYPE_HEATER_STOP:
        return stop_heater();
    default:
        LOGGER_LOG_ERROR(TAG, "Unknown heater command type: %d", data->type);
        return ESP_OK;
    }
}

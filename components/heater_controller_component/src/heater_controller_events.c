#include "heater_controller_internal.h"
#include "utils.h"
#include "event_manager.h"

static const char* TAG = "HEATER_CTRL_EVENTS";

static void heater_controller_event_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    heater_controller_context_t* ctx = (heater_controller_context_t*)handler_arg;
    switch (id)
    {
    case HEATER_CONTROLLER_SET_POWER_LEVEL:
        {
            if (event_data == NULL || sizeof(float) != sizeof(event_data))
            {
                LOGGER_LOG_WARN(TAG, "Invalid event data for SET_POWER_LEVEL");
                break;
            }
            float power_level = *(float*)event_data;
            CHECK_ERR_LOG(set_heater_target_power_level(ctx, power_level),
                          "Failed to set heater target power level");
            LOGGER_LOG_INFO(TAG, "Heater target power level set to %.2f", power_level);
            break;
        }

    case HEATER_CONTROLLER_STATUS_REPORT_REQUESTED:
        {
            LOGGER_LOG_INFO(TAG, "Heater Controller status report requested");
            // Here you would gather status data and post a response event
            // TODO : Implement status report response
            break;
        }

    default:
        LOGGER_LOG_WARN(TAG, "Unknown Heater Controller Event ID: %d", id);
        break;
    }
}

esp_err_t init_events(heater_controller_context_t* ctx)
{
    CHECK_ERR_LOG_RET(event_manager_subscribe(
                          HEATER_CONTROLLER_EVENT,
                          ESP_EVENT_ANY_ID,
                          heater_controller_event_handler,
                          ctx),
                      "Failed to subscribe to heater controller events");
    return ESP_OK;
}

esp_err_t post_heater_controller_error(heater_controller_error_t error_code)
{
    return post_heater_controller_event(HEATER_CONTROLLER_ERROR_OCCURRED, &error_code, sizeof(error_code));
}

esp_err_t post_heater_controller_event(heater_controller_event_t event_type, void* event_data, size_t event_data_size)
{
    CHECK_ERR_LOG_RET(event_manager_post( HEATER_CONTROLLER_EVENT,
                          event_type,
                          event_data,
                          event_data_size,
                          portMAX_DELAY),
                      "Failed to post heater controller event");

    return ESP_OK;
}

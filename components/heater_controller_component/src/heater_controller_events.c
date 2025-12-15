#include "heater_controller_internal.h"
#include "logger_component.h"
#include "utils.h"

static const char *TAG = "HEATER_CTRL_EVENTS";

ESP_EVENT_DEFINE_BASE(HEATER_CONTROLLER_TX_EVENT);
ESP_EVENT_DEFINE_BASE(HEATER_CONTROLLER_RX_EVENT);

static void heater_controller_event_handler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id)
    {
    default:
        LOGGER_LOG_WARN(TAG, "Unknown Heater Controller Event ID: %d", id);
        break;
    }
}

esp_err_t init_events()
{
    CHECK_ERR_LOG_RET(esp_event_handler_register_with(heater_controller_event_loop_handle,
                                                      HEATER_CONTROLLER_RX_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      heater_controller_event_handler,
                                                      NULL),
                      "Failed to register heater controller event handler");
    return ESP_OK;
}

esp_err_t post_heater_controller_error(heater_controller_error_t error_code)
{
    return post_heater_controller_event(HEATER_CONTROLLER_ERROR_OCCURRED, &error_code, sizeof(error_code));
}

esp_err_t post_heater_controller_event(heater_controller_event_t event_type, void *event_data, size_t event_data_size)
{
    CHECK_ERR_LOG_RET(esp_event_post_to(
                          heater_controller_event_loop_handle,
                          HEATER_CONTROLLER_TX_EVENT,
                          event_type,
                          event_data,
                          event_data_size,
                          portMAX_DELAY),
                      "Failed to post heater controller event");

    return ESP_OK;
}
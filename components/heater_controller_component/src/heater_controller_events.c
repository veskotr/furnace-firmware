#include "heater_controller_internal.h"

static const char *TAG = "HEATER_CTRL_EVENTS";

esp_err_t post_heater_controller_error(heater_controller_error_t error_code)
{
    return post_heater_controller_event(HEATER_CONTROLLER_ERROR_OCCURRED, &error_code, sizeof(error_code));
}

esp_err_t post_heater_controller_event(heater_controller_event_t event_type, void *event_data, size_t event_data_size)
{
    CHECK_ERR_LOG_RET(esp_event_post_to(
                          heater_controller_event_loop_handle,
                          HEATER_CONTROLLER_EVENT,
                          event_type,
                          event_data,
                          event_data_size,
                          portMAX_DELAY),
                      "Failed to post heater controller event");

    return ESP_OK;
}
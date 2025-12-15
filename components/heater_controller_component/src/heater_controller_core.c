#include "heater_controller_component.h"
#include "heater_controller_internal.h"
#include "utils.h"
#include "freertos/semphr.h"

static const char *TAG = "HEATER_CTRL_CORE";

float heater_target_power_level = 0.0f;
esp_event_loop_handle_t heater_controller_event_loop_handle = NULL;

esp_err_t set_heater_target_power_level(float power_level)
{
    if (power_level < 0.0f || power_level > 1.0f)
    {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(heater_controller_mutex, portMAX_DELAY);
    heater_target_power_level = power_level;
    xSemaphoreGive(heater_controller_mutex);
    return ESP_OK;
}

esp_err_t init_heater_controller_component(esp_event_loop_handle_t loop_handle)
{
    heater_controller_event_loop_handle = loop_handle;

    CHECK_ERR_LOG_RET(init_heater_controller(), "Failed to initialize heater controller");

    CHECK_ERR_LOG_RET(init_heater_controller_task(), "Failed to initialize heater controller task");

    return ESP_OK;
}

esp_err_t shutdown_heater_controller_component(void)
{
    CHECK_ERR_LOG_RET(shutdown_heater_controller_task(), "Failed to shutdown heater controller task");

    return ESP_OK;
}
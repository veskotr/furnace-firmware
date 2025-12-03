#include "sdkconfig.h"
#include "gpio_master_driver.h"
#include "utils.h"
#include "heater_controller_internal.h"
#include "hal/gpio_types.h"

static const char *TAG = "HEATER_CTRL";
static const int heater_gpio_pull_up = 0;
static const int heater_gpio_pull_down = 1;

esp_err_t init_heater_controller(void)
{
    LOGGER_LOG_INFO(TAG, "Initializing Heater Controller");

    gpio_master_driver_init();

    CHECK_ERR_LOG_RET(gpio_master_set_pin_mode(CONFIG_HEATER_CONTROLLER_GPIO, GPIO_MODE_OUTPUT, heater_gpio_pull_up, heater_gpio_pull_down),
                      "Failed to set heater GPIO pin mode");


    return ESP_OK;
}

esp_err_t toggle_heater(bool state)
{
    LOGGER_LOG_INFO(TAG, "Toggling Heater to state: %s", state ? "ON" : "OFF");

    CHECK_ERR_LOG_RET(gpio_master_set_level(CONFIG_HEATER_CONTROLLER_GPIO, state ? 1 : 0),
                      "Failed to set heater GPIO level");

    return ESP_OK;
}


esp_err_t shutdown_heater_controller(void)
{
    LOGGER_LOG_INFO(TAG, "Shutting down Heater Controller");

    CHECK_ERR_LOG_RET(toggle_heater(HEATER_OFF),
                      "Failed to turn off heater during shutdown");

    return ESP_OK;
}
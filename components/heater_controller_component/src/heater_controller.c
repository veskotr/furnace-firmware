#include "sdkconfig.h"
#include "gpio_master_driver.h"
#include "utils.h"
#include "heater_controller_internal.h"
#include "hal/gpio_types.h"

static const char* TAG = "HEATER_CTRL";
static const int heater_gpio_pull_up = 0;
static const int heater_gpio_pull_down = 1;

esp_err_t init_heater_controller(void)
{
    LOGGER_LOG_INFO(TAG, "Initializing Heater Controller");

    gpio_master_driver_init();

    CHECK_ERR_LOG_RET(
        gpio_master_set_pin_mode(CONFIG_HEATER_CONTACTOR_GPIO_PIN, GPIO_MODE_OUTPUT, heater_gpio_pull_up,
            heater_gpio_pull_down),
        "Failed to set heater GPIO pin mode");

    CHECK_ERR_LOG_RET(toggle_heater(HEATER_OFF), "Failed to turn off heater during initialization");

    CHECK_ERR_LOG_RET(gpio_master_set_pin_mode(CONFIG_HEATER_SSR_GPIO_PIN, GPIO_MODE_OUTPUT, heater_gpio_pull_up,
        heater_gpio_pull_down),
        "Failed to set SSR GPIO pin mode");


    CHECK_ERR_LOG_RET(gpio_master_set_pin_mode(CONFIG_FAN_CONTROL_GPIO_PIN, GPIO_MODE_OUTPUT, heater_gpio_pull_up,
        heater_gpio_pull_down),
        "Failed to set fan GPIO pin mode");

    CHECK_ERR_LOG_RET(stop_heater(), "Failed to stop heater during initialization");
        
    return ESP_OK;
}

esp_err_t toggle_heater(bool state)
{
    LOGGER_LOG_INFO(TAG, "Toggling Heater to state: %s", state ? "ON" : "OFF");

    CHECK_ERR_LOG_RET(gpio_master_set_level(CONFIG_HEATER_SSR_GPIO_PIN, state ? 1 : 0),
                      "Failed to set heater GPIO level");

    post_heater_controller_event(HEATER_CONTROLLER_HEATER_TOGGLED, &state, sizeof(state));

    return ESP_OK;
}


esp_err_t shutdown_heater_controller(void)
{
    LOGGER_LOG_INFO(TAG, "Shutting down Heater Controller");

    CHECK_ERR_LOG_RET(toggle_heater(HEATER_OFF),
                      "Failed to turn off heater during shutdown");

    return ESP_OK;
}

esp_err_t start_heater()
{
    LOGGER_LOG_INFO(TAG, "Starting Heater");

    CHECK_ERR_LOG_RET(gpio_master_set_level(CONFIG_FAN_CONTROL_GPIO_PIN, 1),
                      "Failed to set fan GPIO level to start heater");

    CHECK_ERR_LOG_RET(gpio_master_set_level(CONFIG_HEATER_CONTACTOR_GPIO_PIN, 1),
                      "Failed to set heater contactor GPIO level to start heater");

    return ESP_OK;
}

esp_err_t stop_heater()
{
    LOGGER_LOG_INFO(TAG, "Stopping Heater");

    CHECK_ERR_LOG_RET(gpio_master_set_level(CONFIG_HEATER_CONTACTOR_GPIO_PIN, 0),
                      "Failed to set heater contactor GPIO level to stop heater");

    CHECK_ERR_LOG_RET(gpio_master_set_level(CONFIG_FAN_CONTROL_GPIO_PIN, 0),
                      "Failed to set fan GPIO level to stop heater");

    return ESP_OK;
}

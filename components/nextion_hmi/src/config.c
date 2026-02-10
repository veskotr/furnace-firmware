#include "config.h"

#include "app_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "config";

void config_init(void)
{
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return;
    }

    // User config removed; defaults only.
}

AppConfig config_get_defaults(void)
{
    AppConfig cfg = {
        .max_operational_time_min = CONFIG_MAX_OPERATIONAL_TIME_MIN,
        .min_operational_time_min = CONFIG_MIN_OPERATIONAL_TIME_MIN,
        .max_temperature_c = CONFIG_MAX_TEMPERATURE_C,
        .sensor_read_frequency_sec = CONFIG_SENSOR_READ_FREQUENCY_SEC,
        .delta_t_max_per_min_x10 = CONFIG_DELTA_T_MAX_PER_MIN_X10,
        .delta_t_min_per_min_x10 = CONFIG_DELTA_T_MIN_PER_MIN_X10,
        .time_tolerance_sec = CONFIG_TIME_TOLERANCE_SEC,
        .temp_tolerance_c = CONFIG_TEMP_TOLERANCE_C,
        .delta_temp_tolerance_c_x10 = CONFIG_DELTA_TEMP_TOLERANCE_C_X10,
        .power_kw = CONFIG_HEATER_POWER,
    };
    return cfg;
}

AppConfig config_get_effective(void)
{
    return config_get_defaults();
}

#include "logger_component.h"
#include "commands_dispatcher.h"
#include "temperature_processor_component.h"
#include "coordinator_component.h"
#include "heater_controller_component.h"
#include "event_manager.h"
#include "event_registry.h"
#include "nextion_hmi.h"
#include "run_indicator.h"
#include "utils.h"
#include "sdkconfig.h"
#include "health_monitor.h"
#include "device_manager.h"
#include "esp_log.h"
#include "modbus_master.h"
#include "temp_sensor_device.h"
#include "nvs_flash.h"
#include "debug_console.h"

static const char *TAG = "main";

void app_main(void)
{
    const esp_err_t debug_console_err = debug_console_init();
    if (debug_console_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize debug console: %s", esp_err_to_name(debug_console_err));
    }

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK)
    {
        LOGGER_LOG_ERROR(TAG, "NVS flash init failed: %s", esp_err_to_name(nvs_err));
    }

    logger_init();

    CHECK_ERR_LOG_CALL(event_manager_init(),
                       return,
                       "Failed to initialize event manager");
    CHECK_ERR_LOG_CALL(event_registry_init(),
                       return,
                       "Failed to initialize event registry");
    CHECK_ERR_LOG_CALL(commands_dispatcher_init(),
                       return,
                       "Failed to initialize commands dispatcher");
    CHECK_ERR_LOG_CALL(init_heater_controller_component(),
                       return,
                       "Failed to initialize heater controller");
    CHECK_ERR_LOG_CALL(init_coordinator(),
                       return,
                       "Failed to initialize coordinator");

    CHECK_ERR_LOG(init_health_monitor(),
                  "Failed to initialize health monitor");

    const modbus_config_t modbus_config = {
        .uart_num = CONFIG_DEVICE_MANAGER_MODBUS_UART_NUMBER,
        .tx_pin = CONFIG_DEVICE_MANAGER_MODBUS_TX_PIN,
        .rx_pin = CONFIG_DEVICE_MANAGER_MODBUS_RX_PIN,
        .de_pin = CONFIG_DEVICE_MANAGER_DE_PIN,
        .baud_rate = CONFIG_DEVICE_MANAGER_MODBUS_BAUD_RATE,
    };

    CHECK_ERR_LOG(modbus_master_init(&modbus_config),
                  "Failed to initialize Modbus master");

    run_indicator_init();

    nextion_hmi_init();


    CHECK_ERR_LOG(device_manager_init(),
                  "Failed to initialize device manager");

    vTaskDelay(pdMS_TO_TICKS(2000)); // Let the device manager start and initialize devices

    CHECK_ERR_LOG(temperature_processor_init(),
                  "Failed to initialize temperature processor");
   
    LOGGER_LOG_INFO(TAG, "System initialized successfully");
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

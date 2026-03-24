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
#include "modbus_master.h"
#include "temp_sensor_device.h"
#include "nvs_flash.h"

static const char *TAG = "main";

void app_main(void)
{
    logger_init();

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

    LOGGER_LOG_INFO(TAG, "System initialized successfully");

    CHECK_ERR_LOG(device_manager_init(),
                  "Failed to initialize device manager");

    vTaskDelay(pdMS_TO_TICKS(2000)); // Let the device manager start and initialize devices

    temp_sensor_device_t *temp_sensor_device;

    CHECK_ERR_LOG(temp_sensor_create(&temp_sensor_device),
                  "Failed to create temp sensor device");

    vTaskDelay(pdMS_TO_TICKS(2000));

    CHECK_ERR_LOG(temp_sensor_set_device_state(temp_sensor_device, DEVICE_STATE_RUNNING),
                  "Failed to set temp sensor device state to running");

    while (1)
    {
        float temperature;
        CHECK_ERR_LOG(temp_sensor_read_device(temp_sensor_device, &temperature),
                      "Failed to read temperature from device");
        CHECK_ERR_LOG(event_manager_post_immediate(TEMP_PROCESSOR_EVENT,
                                                   PROCESS_TEMPERATURE_EVENT_DATA,
                                                   &temperature,
                                                   sizeof(temperature)),
                      "Failed to publish temperature update");
        LOGGER_LOG_INFO(TAG, "Temperature: %.2f C", temperature);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

#include "logger_component.h"
#include "temperature_processor_component.h"
#include "coordinator_component.h"
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

#ifdef CONFIG_MODBUS_TEMP_ENABLED
#include "modbus_temp_monitor.h"
#endif

static const char* TAG = "main";

void app_main(void)
{
    logger_init();

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "NVS flash init failed: %s", esp_err_to_name(nvs_err));
    }

    CHECK_ERR_LOG_CALL(event_manager_init(),
                       return,
                       "Failed to initialize event manager");
    CHECK_ERR_LOG_CALL(event_registry_init(),
                       return,
                       "Failed to initialize event registry");

/*     temp_monitor_config_t temp_monitor_config = {
    bool sensors_available = false;

#ifdef CONFIG_MODBUS_TEMP_ENABLED
    /* ── MODBUS path: MS9024 provides temperature via RS485 ────────── */
    LOGGER_LOG_WARN(TAG, "*** MODBUS temperature source (MS9024) ***");
    LOGGER_LOG_WARN(TAG, "SPI temperature monitor and processor SKIPPED");
    sensors_available = true;   /* MODBUS component posts events directly */
#else
    /* ── SPI path: MAX31865 thermocouples ──────────────────────────── */
    temp_monitor_config_t temp_monitor_config = {
        .number_of_attached_sensors = 5
    };
    esp_err_t temp_err = init_temp_monitor(&temp_monitor_config);
    if (temp_err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "Failed to initialize temperature monitor: %s (continuing without sensors)",
                         esp_err_to_name(temp_err));
    } else {
        sensors_available = true;
    }

    if (sensors_available) {
        CHECK_ERR_LOG_CALL(init_temp_processor(),
                           return,
                           "Failed to initialize temperature processor");
    } else {
        LOGGER_LOG_WARN(TAG, "Skipping temperature processor init (no sensors)");
    }
#endif

    CHECK_ERR_LOG_CALL(init_coordinator(),
                       return,
                       "Failed to initialize coordinator");

    CHECK_ERR_LOG(init_health_monitor(),
                  "Failed to initialize health monitor"); */

    const modbus_config_t modbus_config = {
        .uart_num = CONFIG_DEVICE_MANAGER_MODBUS_UART_NUMBER,
        .tx_pin = CONFIG_DEVICE_MANAGER_MODBUS_TX_PIN,
        .rx_pin = CONFIG_DEVICE_MANAGER_MODBUS_RX_PIN,
        .de_pin = CONFIG_DEVICE_MANAGER_DE_PIN,
        .baud_rate = CONFIG_DEVICE_MANAGER_MODBUS_BAUD_RATE,
    };

    CHECK_ERR_LOG(modbus_master_init(&modbus_config),
                           "Failed to initialize Modbus master");
#ifdef CONFIG_MODBUS_TEMP_ENABLED
    /* Start MODBUS monitor AFTER coordinator (needs event base ready) */
    CHECK_ERR_LOG_CALL(modbus_temp_monitor_init(),
                       return,
                       "Failed to initialize MODBUS temperature monitor");
#endif

    if (sensors_available) {
#ifndef CONFIG_MODBUS_TEMP_ENABLED
        /* Health monitor expects heartbeats from temp_monitor/processor;
           those don't exist in MODBUS mode, so only init for SPI path. */
        CHECK_ERR_LOG_CALL(init_health_monitor(),
                           return,
                           "Failed to initialize health monitor");
#else
        LOGGER_LOG_WARN(TAG, "Health monitor skipped in MODBUS mode (no temp_monitor heartbeats)");
#endif
    } else {
        LOGGER_LOG_WARN(TAG, "Skipping health monitor init (temp sensors unavailable, WDT would trigger)");
    }

    run_indicator_init();

    nextion_hmi_init();

    LOGGER_LOG_INFO(TAG, "System initialized successfully");

    CHECK_ERR_LOG(device_manager_init(),
                  "Failed to initialize device manager");

    vTaskDelay(pdMS_TO_TICKS(2000)); // Let the device manager start and initialize devices

    temp_sensor_device_t *temp_sensor_device;

    CHECK_ERR_LOG(temp_sensor_create(&temp_sensor_device),
                  "Failed to create temp sensor device");

    CHECK_ERR_LOG(temp_sensor_set_device_state(temp_sensor_device, DEVICE_STATE_IDLE),
                  "Failed to set temp sensor device state to running");



    device_write_cmd_t write_cmd = {
        .cmd_id = TEMP_SENSOR_REPAIR_FORM_GOOD_UNIT,
        .params = NULL
    };

    CHECK_ERR_LOG(temp_sensor_write_device(temp_sensor_device, &write_cmd),
        "Failed to write to temp sensor device");

    /*vTaskDelay(pdMS_TO_TICKS(2000));
    params.register_address = 32;
    params.value = 4;

    CHECK_ERR_LOG(device_manager_write_device(temp_sensor_device, &write_cmd),
        "Failed to write to temp sensor device");*/


    vTaskDelay(pdMS_TO_TICKS(2000));

    CHECK_ERR_LOG(temp_sensor_set_device_state(temp_sensor_device, DEVICE_STATE_RUNNING),
                  "Failed to set temp sensor device state to running");

    while (1)
    {
        float temperature;
        CHECK_ERR_LOG(temp_sensor_read_device(temp_sensor_device, &temperature),
                      "Failed to read temperature from device");
        LOGGER_LOG_INFO(TAG, "Temperature: %.1f C", temperature);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

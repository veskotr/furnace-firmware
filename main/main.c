#include "logger_component.h"
#include "temperature_monitor_component.h"
#include "temperature_processor_component.h"
#include "coordinator_component.h"
#include "event_manager.h"
#include "utils.h"
#include "sdkconfig.h"
#include "health_monitor.h"
#include "device_manager.h"
#include "modbus_master.h"
#include "temp_sensor_device.h"

static const char* TAG = "main";

void app_main(void)
{
    logger_init();
    CHECK_ERR_LOG(event_manager_init(),
                  "Failed to initialize event manager");

/*     temp_monitor_config_t temp_monitor_config = {
        .number_of_attached_sensors = 5
    };
    CHECK_ERR_LOG(init_temp_monitor(&temp_monitor_config),
                  "Failed to initialize temperature monitor");

    CHECK_ERR_LOG(init_temp_processor(),
                  "Failed to initialize temperature processor");

    const coordinator_config_t coordinator_config = {
        .profiles = NULL,
        .num_profiles = 0
    };
    CHECK_ERR_LOG(init_coordinator(&coordinator_config),
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

    LOGGER_LOG_INFO(TAG, "System initialized successfully");

    CHECK_ERR_LOG(device_manager_init(),
                  "Failed to initialize device manager");

    vTaskDelay(pdMS_TO_TICKS(2000)); // Let the device manager start and initialize devices

    device_t *temp_sensor_device;

    CHECK_ERR_LOG(temp_sensor_create(&temp_sensor_device),
                  "Failed to create temp sensor device");

    CHECK_ERR_LOG(device_manager_set_device_state(temp_sensor_device, DEVICE_STATE_IDLE),
                  "Failed to set temp sensor device state to running");



    device_write_cmd_t write_cmd = {
        .cmd_id = TEMP_SENSOR_REPAIR_FORM_GOOD_UNIT,
        .params = NULL
    };

    CHECK_ERR_LOG(device_manager_write_device(temp_sensor_device, &write_cmd),
        "Failed to write to temp sensor device");

    /*vTaskDelay(pdMS_TO_TICKS(2000));
    params.register_address = 32;
    params.value = 4;

    CHECK_ERR_LOG(device_manager_write_device(temp_sensor_device, &write_cmd),
        "Failed to write to temp sensor device");*/


    vTaskDelay(pdMS_TO_TICKS(2000));

    CHECK_ERR_LOG(device_manager_set_device_state(temp_sensor_device, DEVICE_STATE_RUNNING),
                  "Failed to set temp sensor device state to running");

    while (1)
    {
        float temperature;
        CHECK_ERR_LOG(device_manager_read_device(temp_sensor_device, &temperature),
                      "Failed to read temperature from device");
        LOGGER_LOG_INFO(TAG, "Temperature: %.1f C", temperature);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

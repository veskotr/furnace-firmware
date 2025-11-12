#include "temperature_monitor_task.h"
#include "temperature_monitor_component.h"
#include "temperature_sensors.h"
#include "temperature_monitor_internal_types.h"

#include "esp_event.h"
#include "logger_component.h"

// Task handle
TaskHandle_t temp_monitor_task_handle;

temp_monitor_t temp_monitor = {0};

temp_sensors_array_t temp_sensors_array = {0};

static temp_sensor_fault_type_t classify_sensor_fault(temp_sensor_t *sensor_data);

static void post_temp_monitor_error(temp_monitor_error_t type, esp_err_t esp_err_code);

static temp_monitor_error_t map_esp_err_to_temp_monitor_error(esp_err_t err);

static esp_err_t post_measured_temperature_event();

// ----------------------------
// Task
// ----------------------------
static void temp_monitor_task(void *args)
{
    LOGGER_LOG_INFO(TAG, "Temperature monitor task started");

    while (monitor_running)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Read temperature sensors data
        esp_err_t ret = read_temp_sensors_data(temp_sensors_array.sensors);

        // Handle read error
        if (ret != ESP_OK)
        {
            post_temp_monitor_error(map_esp_err_to_temp_monitor_error(ret), ret);
            LOGGER_LOG_ERROR(TAG, "Failed to get temperatures: %s", esp_err_to_name(ret));
            continue;
        }

        // Send temperature event
        ret = post_measured_temperature_event();

        if (ret != ESP_OK)
        {
            LOGGER_LOG_ERROR(TAG, "Failed to post temperature event: %s", esp_err_to_name(ret));
        }
    }

    LOGGER_LOG_INFO(TAG, "Temperature monitor task exiting");
    vTaskDelete(NULL);
}


// Classify sensor error based on internal sensor data
static temp_sensor_fault_type_t classify_sensor_fault(temp_sensor_t *sensor_data)
{
    if (sensor_data->sensor_fault.raw_fault_byte == 0)
    {
        return SENSOR_ERR_NONE;
    }
    else if (sensor_data->sensor_fault.faults.rtdin_force_open || sensor_data->sensor_fault.faults.refin_force_open || sensor_data->sensor_fault.faults.refin_force_closed)
    {
        return SENSOR_ERR_RTD_FAULT;
    }
    else if (sensor_data->sensor_fault.faults.over_under_voltage)
    {
        return SENSOR_ERR_COMMUNICATION;
    }
    else
    {
        return SENSOR_ERR_UNKNOWN;
    }
}

// Post measured temperature event
static esp_err_t post_measured_temperature_event()
{
    esp_err_t ret = esp_event_post_to(
        temp_monitor.temperature_event_loop_handle,
        MEASURED_TEMPERATURE_EVENT,
        TEMP_MEASUREMENT_SENSOR_DATA_EVENT,
        &temp_sensors_array,
        sizeof(temp_sensors_array),
        portMAX_DELAY);
    return ret;
}

// Post temperature monitor error event
static void post_temp_monitor_error(temp_monitor_error_t type, esp_err_t esp_err_code)
{
    temp_monitor_error_data_t error_data = {
        .type = type,
        .esp_err_code = esp_err_code};

    esp_err_t ret = esp_event_post_to(
        temp_monitor.coordinator_event_loop_handle,
        TEMP_MEASURE_ERROR_EVENT,
        type,
        &error_data,
        sizeof(error_data),
        portMAX_DELAY);

    if (ret != ESP_OK)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to post temperature monitor error event: %s", esp_err_to_name(ret));
    }
}

//Map esp_err_t to temp_monitor_error_data_t
static temp_monitor_error_t map_esp_err_to_temp_monitor_error(esp_err_t err)
{
    switch (err) {
    case ESP_ERR_INVALID_ARG:
    case ESP_ERR_INVALID_STATE:
        return TEMP_MONITOR_ERR_SENSOR_READ;
    case ESP_ERR_TIMEOUT:
        return TEMP_MONITOR_ERR_SPI;
    default:
        return TEMP_MONITOR_ERR_UNKNOWN;
    }
}

#ifndef TEMPERATURE_MONITOR_COMPONENT_H
#define TEMPERATURE_MONITOR_COMPONENT_H

#include "esp_err.h"
#include "esp_event.h"
#include <inttypes.h>
#include "sdkconfig.h"

ESP_EVENT_DECLARE_BASE(MEASURED_TEMPERATURE_EVENT);
ESP_EVENT_DECLARE_BASE(TEMP_MEASURE_ERROR_EVENT);

typedef enum
{
    TEMP_MEASUREMENT_SENSOR_DATA_EVENT = 0
} temp_measurement_event_t;

typedef enum
{
    TEMP_MONITOR_ERR_NONE = 0,
    TEMP_MONITOR_ERR_SENSOR_READ,
    TEMP_MONITOR_ERR_SENSOR_FAULT,
    TEMP_MONITOR_ERR_SPI,
    TEMP_MONITOR_ERR_UNKNOWN
} temp_monitor_error_t;

typedef struct
{
    temp_monitor_error_t type;
    esp_err_t esp_err_code;
} temp_monitor_error_data_t;

typedef enum
{
    SENSOR_ERR_NONE = 0,
    SENSOR_ERR_RTD_FAULT,
    SENSOR_ERR_COMMUNICATION,
    SENSOR_ERR_UNKNOWN
} temp_sensor_fault_type_t;

typedef struct
{
    bool high_threshold;
    bool low_threshold;
    bool refin_force_closed;
    bool refin_force_open;
    bool rtdin_force_open;
    bool over_under_voltage;
} max31865_fault_flags_t;

typedef struct
{
    temp_sensor_fault_type_t type;
    max31865_fault_flags_t faults;
    uint8_t raw_fault_byte;
} temp_sensor_fault_t;

typedef struct
{
    uint8_t index;               // Sensor index
    float temperature_c;         // Last measured temperature
    bool valid;                  // Whether the data is valid (no fault, good comms)
    uint32_t last_update_ms;     // Timestamp of last successful update
    temp_sensor_fault_t sensor_fault; // Sensor fault information
} temp_sensor_t;

typedef struct
{
    temp_sensor_t sensors[CONFIG_TEMP_SENSORS_MAX_SENSORS];
    uint8_t number_of_attached_sensors;
} temp_sensors_array_t;

typedef struct
{
    uint8_t number_of_attached_sensors;
    esp_event_loop_handle_t temperature_events_loop_handle;
    esp_event_loop_handle_t coordinator_events_loop_handle;
} temp_monitor_config_t;

esp_err_t init_temp_monitor(temp_monitor_config_t *config);

esp_err_t shutdown_temp_monitor_controller(void);

#endif // TEMPERATURE_MONITOR_COMPONENT_H
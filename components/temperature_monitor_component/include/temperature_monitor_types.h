#ifndef TEMPERATURE_MONITOR_TYPES_H
#define TEMPERATURE_MONITOR_TYPES_H

#include "esp_err.h"
#include "esp_event.h"
#include <inttypes.h>
#include "sdkconfig.h"

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
    uint8_t index;                    // Sensor index
    float temperature_c;              // Last measured temperature
    bool valid;                       // Whether the data is valid (no fault, good comms)
    temp_sensor_fault_t sensor_fault; // Sensor fault information
} temp_sensor_t;

typedef struct
{
    temp_sensor_t sensors[CONFIG_TEMP_SENSORS_MAX_SENSORS];
    uint8_t number_of_attached_sensors;
    uint32_t timestamp_ms;
    bool valid;
} temp_sample_t;

#endif // TEMPERATURE_MONITOR_TYPES_H
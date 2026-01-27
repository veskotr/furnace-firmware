#ifndef TEMPERATURE_MONITOR_TYPES_H
#define TEMPERATURE_MONITOR_TYPES_H

#include "sdkconfig.h"
#include <stdbool.h>
#include <inttypes.h>
#include "esp_err.h"

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
    uint8_t index; // Sensor index
    float temperature_c; // Last measured temperature
    bool valid; // Whether the data is valid (no fault, good comms)
    uint8_t raw_fault_byte;
    esp_err_t error;
} temp_sensor_t;

typedef struct
{
    uint32_t timestamp_ms;
    temp_sensor_t sensors[CONFIG_TEMP_SENSORS_MAX_SENSORS];
    uint8_t number_of_attached_sensors;
    bool valid;
    bool empty;
} temp_sample_t;

#endif // TEMPERATURE_MONITOR_TYPES_H

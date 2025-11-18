#ifndef TEMPERATURE_SENSORS_H
#define TEMPERATURE_SENSORS_H

#include "esp_err.h"
#include <inttypes.h>
#include "temperature_monitor_types.h"
#include "temperature_monitor_log.h"

// Component tag

esp_err_t init_temp_sensors(void);

esp_err_t read_temp_sensors_data(temp_sensor_t *data_buffer);

// static esp_err_t get_

#endif // TEMPERATURE_SENSORS_H
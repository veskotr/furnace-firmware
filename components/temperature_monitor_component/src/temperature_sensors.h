#ifndef TEMPERATURE_SENSORS_H
#define TEMPERATURE_SENSORS_H

#include "esp_err.h"
#include "temperature_monitor_types.h"

esp_err_t init_temp_sensors(void);

esp_err_t read_temp_sensors_data(temp_sample_t *temp_sample_to_fill);

// static esp_err_t get_

#endif // TEMPERATURE_SENSORS_H
#ifndef TEMPERATURE_SENSORS_H
#define TEMPERATURE_SENSORS_H

#include "esp_err.h"
#include "esp_event.h"
#include <inttypes.h>

// Component tag
static const char *TAG = "TEMP_MONITOR";

typedef struct
{
    uint8_t number_of_attatched_sensors;
    esp_event_loop_handle_t temperature_event_loop_handle;
    esp_event_loop_handle_t coordinator_event_loop_handle;

} temp_monitor_t;

extern temp_monitor_t temp_monitor;

static esp_err_t init_temp_sensors(void);

static esp_err_t init_temp_sensor(uint8_t sensor_index);

static esp_err_t read_temp_sensors_data(uint16_t *data_buffer, size_t *buffer_len);

static esp_err_t read_temperature_sensor(uint8_t sensor_index, float *temperature);

static esp_err_t process_temperature_data(uint16_t sensor_data, float *temperature);

#endif // TEMPERATURE_SENSORS_H
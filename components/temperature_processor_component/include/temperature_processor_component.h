#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stdbool.h>

typedef struct
{
    float average_temperature;
    TickType_t sample_tick;
    uint8_t valid_sensor_count;
    uint8_t total_sensor_count;
    bool valid;
} temperature_processor_sample_t;

esp_err_t init_temp_processor(uint8_t number_of_temp_sensors);

esp_err_t shutdown_temp_processor(void);

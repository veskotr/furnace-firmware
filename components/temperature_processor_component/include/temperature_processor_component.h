#pragma once

#include "esp_err.h"

esp_err_t init_temp_processor(uint8_t number_of_temp_sensors);

esp_err_t shutdown_temp_processor(void);

#pragma once

#include "esp_err.h"
#include "esp_event.h"

esp_err_t init_heater_controller_component(void);

esp_err_t shutdown_heater_controller_component(void);


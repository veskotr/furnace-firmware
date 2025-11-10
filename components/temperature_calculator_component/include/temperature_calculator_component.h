#ifndef TEMPERATURE_CALC_COMPONENT_H
#define TEMPERATURE_CALC_COMPONENT_H

#include "esp_err.h"
#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(CALC_TEMPERATURE_EVENT);

esp_err_t init_temp_calculator(esp_event_loop_handle_t loop_handle);

esp_err_t shutdown_temp_calc_controller(void);

#endif
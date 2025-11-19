#ifndef HEATER_CONTROLLER_COMPONENT_H
#define HEATER_CONTROLLER_COMPONENT_H

#include "esp_err.h"
#include "esp_event.h"

esp_err_t init_heater_controller(esp_event_loop_handle_t loop_handle);
esp_err_t set_heater_target_power_level(float power_level);
esp_err_t shutdown_heater_controller(void);

#endif // HEATER_CONTROLLER_COMPONENT_H
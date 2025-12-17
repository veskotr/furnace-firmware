#ifndef HEATER_CONTROLLER_COMPONENT_H
#define HEATER_CONTROLLER_COMPONENT_H

#include "esp_err.h"
#include "esp_event.h"

esp_err_t init_heater_controller_component(void);
esp_err_t shutdown_heater_controller_component(void);

#endif // HEATER_CONTROLLER_COMPONENT_H
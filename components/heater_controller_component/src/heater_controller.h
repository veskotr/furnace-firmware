#ifndef HEATER_CONTROLLER_H
#define HEATER_CONTROLLER_H

#include "esp_err.h"
#include <stdbool.h>

static const bool HEATER_ON = true;
static const bool HEATER_OFF = false;

esp_err_t init_heater_controller(void);

esp_err_t toggle_heater(bool state);

esp_err_t shutdown_heater_controller(void);
#endif // HEATER_CONTROLLER_H
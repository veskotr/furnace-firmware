#pragma once

#include "esp_err.h"
#include "esp_event.h"

esp_err_t init_heater_controller_component(void);

esp_err_t shutdown_heater_controller_component(void);

/* Recoverable sensor-data gate. This never clears the permanent hardware
 * inhibit latched by an SSR/GPIO failure. */
esp_err_t heater_controller_set_sensor_data_inhibit(bool inhibited);
/* Temporary coordinator gate used for pause/stop/completion. */
esp_err_t heater_controller_set_control_inhibit(bool inhibited);
bool heater_controller_output_is_inhibited(void);

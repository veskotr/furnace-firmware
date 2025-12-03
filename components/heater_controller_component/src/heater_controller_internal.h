#ifndef HEATER_CONTROLLER_INTERNAL_H
#define HEATER_CONTROLLER_INTERNAL_H

#include "esp_err.h"
#include <stdbool.h>
#include "esp_event.h"
#include "freertos/semphr.h"
#include "heater_controller_types.h"

extern bool heater_controller_task_running;
extern float heater_target_power_level;
extern esp_event_loop_handle_t heater_controller_event_loop_handle;
extern SemaphoreHandle_t heater_controller_mutex;

static const bool HEATER_ON = true;
static const bool HEATER_OFF = false;

// ----------------------------
// Task management functions
// ----------------------------
esp_err_t init_heater_controller_task(void);
esp_err_t shutdown_heater_controller_task(void);

// ----------------------------
// Hardware control functions
// ----------------------------
esp_err_t init_heater_controller(void);
esp_err_t toggle_heater(bool state);
esp_err_t shutdown_heater_controller(void);


// ----------------------------
// Event posting functions
// ----------------------------
esp_err_t post_heater_controller_error(heater_controller_error_t error_code);
esp_err_t post_heater_controller_event(heater_controller_event_t event_type, void *event_data, size_t event_data_size);


#endif // HEATER_CONTROLLER_INTERNAL_H
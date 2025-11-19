#ifndef HEATER_CONTROLLER_TASK_H
#define HEATER_CONTROLLER_TASK_H

#include "esp_err.h"
#include <stdbool.h>
#include "esp_event.h"
#include "freertos/semphr.h"

extern bool heater_controller_task_running;
extern float heater_target_power_level;
extern esp_event_loop_handle_t heater_controller_event_loop_handle;
extern SemaphoreHandle_t heater_controller_mutex;

esp_err_t init_heater_controller_task(void);
esp_err_t shutdown_heater_controller_task(void);

#endif // HEATER_CONTROLLER_TASK_H
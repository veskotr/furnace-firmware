#ifndef COORDINATOR_COMPONENT_TASK_H
#define COORDINATOR_COMPONENT_TASK_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "core_types.h"

extern TaskHandle_t coordinator_task_handle;
extern float coordinator_current_temperature;
extern bool coordinator_running;
extern volatile bool profile_paused;
extern heating_profile_t *coordinator_heating_profiles;
extern size_t number_of_profiles;

esp_err_t init_coordinator_task(void);
esp_err_t shutdown_coordinator_task(void);

#endif // COORDINATOR_COMPONENT_TASK_H
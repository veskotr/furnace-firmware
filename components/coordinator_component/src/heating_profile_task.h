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

esp_err_t start_heating_profile(const int profile_index);

esp_err_t pause_heating_profile(void);

esp_err_t resume_heating_profile(void);

void get_heating_task_state(heating_task_state_t *state_out);

void get_current_heating_profile(size_t *profile_index_out);

esp_err_t stop_heating_profile(void);

#endif // COORDINATOR_COMPONENT_TASK_H
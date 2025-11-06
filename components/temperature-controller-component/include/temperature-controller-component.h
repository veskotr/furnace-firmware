#ifndef TEMPERATURE_CONTROLLER_COMPONENT_H
#define TEMPERATURE_CONTROLLER_COMPONENT_H

#include "esp_err.h"
#include "esp_event.h"
#include "core_types.h"

ESP_EVENT_DECLARE_BASE(MESURED_TEMPERATURE_EVENT);
ESP_EVENT_DECLARE_BASE(SET_TEMPERATURE_EVENT);

static SemaphoreHandle_t mesured_temp_mutex;
static SemaphoreHandle_t set_temp_mutex;

esp_err_t init_temp_mesure_controller(esp_event_loop_handle_t loop_handle);
esp_err_t init_temp_set_controller(esp_event_loop_handle_t loop_handle, heating_program_t heating_program);

esp_err_t shutdown_temp_mesure_controller();
esp_err_t shutdown_temp_set_controller();

#endif
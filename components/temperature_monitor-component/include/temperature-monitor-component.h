#ifndef TEMPERATURE_MONITOR_COMPONENT_H
#define TEMPERATURE_MONITOR_COMPONENT_H

#include "esp_err.h"
#include "esp_event.h"
#include <inttypes.h>

ESP_EVENT_DECLARE_BASE(MESURED_TEMPERATURE_EVENT);

esp_err_t init_temp_monitor(esp_event_loop_handle_t loop_handle);

esp_err_t shutdown_temp_monitop_controller(void);

#endif
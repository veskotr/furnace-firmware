#ifndef TEMPERATURE_MONITOR_COMPONENT_H
#define TEMPERATURE_MONITOR_COMPONENT_H

#include "esp_err.h"
#include "esp_event.h"
#include <inttypes.h>

ESP_EVENT_DECLARE_BASE(MESURED_TEMPERATURE_EVENT);

typedef struct {
    uint8_t number_of_attatched_sensors;
    esp_event_loop_handle_t temperature_events_loop_handle;
    esp_event_loop_handle_t coordinator_events_loop_handle;
} temp_monitor_config_t;

esp_err_t init_temp_monitor(temp_monitor_config_t *config);

esp_err_t shutdown_temp_monitop_controller(void);

#endif // TEMPERATURE_MONITOR_COMPONENT_H
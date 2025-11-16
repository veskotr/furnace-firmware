#ifndef TEMPERATURE_MONITOR_COMPONENT_H
#define TEMPERATURE_MONITOR_COMPONENT_H

#include "esp_err.h"
#include "esp_event.h"
#include <inttypes.h>
#include "temperature_monitor_types.h"
#define TEMP_READY_EVENT_BIT (1 << 0)

extern EventGroupHandle_t temp_processor_event_group;

typedef struct
{
    uint8_t number_of_attached_sensors;
    esp_event_loop_handle_t temperature_events_loop_handle;
    esp_event_loop_handle_t coordinator_events_loop_handle;
} temp_monitor_config_t;

esp_err_t init_temp_monitor(temp_monitor_config_t *config);

esp_err_t shutdown_temp_monitor_controller(void);

size_t temp_ring_buffer_pop_all(
    temp_sample_t *out_dest,
    size_t max_out);

#endif // TEMPERATURE_MONITOR_COMPONENT_H
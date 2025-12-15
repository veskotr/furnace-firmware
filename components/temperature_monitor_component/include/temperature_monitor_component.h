#ifndef TEMPERATURE_MONITOR_COMPONENT_H
#define TEMPERATURE_MONITOR_COMPONENT_H

#include "esp_err.h"
#include "esp_event.h"
#include <inttypes.h>
#include "temperature_monitor_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define TEMP_READY_EVENT_BIT (1 << 0)

// Accessor for event group (hides internal context)
EventGroupHandle_t temp_monitor_get_event_group(void);

typedef struct
{
    uint8_t number_of_attached_sensors;
} temp_monitor_config_t;

esp_err_t init_temp_monitor(temp_monitor_config_t *config);

esp_err_t shutdown_temp_monitor(void);

size_t temp_ring_buffer_pop_all(
    temp_sample_t *out_dest,
    size_t max_out);

#endif // TEMPERATURE_MONITOR_COMPONENT_H
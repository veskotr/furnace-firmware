#ifndef TEMPERATURE_MONITOR_INTERNAL_TYPES_H
#define TEMPERATURE_MONITOR_INTERNAL_TYPES_H
#include "esp_event.h"
#include <inttypes.h>


typedef struct
{
    uint8_t number_of_attached_sensors;
    esp_event_loop_handle_t temperature_event_loop_handle;
    esp_event_loop_handle_t coordinator_event_loop_handle;

} temp_monitor_t;

extern temp_monitor_t temp_monitor;

// ----------------------------
// Configuration
// ----------------------------
typedef struct
{
    const char *task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} TempMonitorConfig_t;

static const TempMonitorConfig_t temp_monitor_config;

#endif // TEMPERATURE_MONITOR_INTERNAL_TYPES_H
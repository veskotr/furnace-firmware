#ifndef TEMPERATURE_MONITOR_INTERNAL_H
#define TEMPERATURE_MONITOR_INTERNAL_H

#include "esp_err.h"
#include "temperature_monitor_types.h"
#include "temperature_monitor_component.h"
#include "temperature_monitor_types.h"
#include "esp_event.h"
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// Ring buffer structure
typedef struct
{
    temp_sample_t buffer[CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE];
    size_t write_index;
    size_t read_index;
    size_t count;
    SemaphoreHandle_t mutex;
} temp_ring_buffer_t;

// Consolidated context - all component state in one structure
typedef struct
{
    // Configuration
    uint8_t number_of_attached_sensors;
    
    // Task management
    bool monitor_running;
    TaskHandle_t task_handle;
    
    // Event synchronization
    EventGroupHandle_t processor_event_group;
    
    // Ring buffer
    temp_ring_buffer_t ring_buffer;
    
    // Current sample buffer
    temp_sample_t current_sample;
} temp_monitor_context_t;

// Single global context pointer (internal to component)
extern temp_monitor_context_t *g_temp_monitor_ctx;

// ----------------------------
// Configuration
// ----------------------------
typedef struct
{
    const char *task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} TempMonitorConfig_t;

esp_err_t start_temperature_monitor_task(temp_monitor_context_t *ctx);

esp_err_t stop_temperature_monitor_task(temp_monitor_context_t *ctx);

esp_err_t init_temp_sensors(temp_monitor_context_t *ctx);

esp_err_t read_temp_sensors_data(temp_monitor_context_t *ctx, temp_sample_t *temp_sample_to_fill);

bool temp_ring_buffer_init();

void temp_ring_buffer_push(temp_ring_buffer_t *rb, const temp_sample_t *sample);

#endif // TEMPERATURE_MONITOR_INTERNAL_H
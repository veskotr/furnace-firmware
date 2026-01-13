#ifndef TEMPERATURE_MONITOR_INTERNAL_H
#define TEMPERATURE_MONITOR_INTERNAL_H

#include "esp_err.h"
#include "temperature_monitor_types.h"
#include "temperature_monitor_component.h"
#include "esp_event.h"
#include <inttypes.h>

#include "furnace_error_types.h"
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

/*TODO general temp monitor error code
[31:24] Error types flags      (1 byte)
[23:16] Count of sensor read errors  (1 byte)
[15:8]  Count of too many bad samples (1 byte)
[7:0]   Count of over temp occurrences (1 byte)
*/
// Error types
typedef enum
{
    TEMP_MONITOR_HW_ERROR,
    TEMP_MONITOR_DATA_ERROR,
    TEMP_MONITOR_ERROR_UNKNOWN = 0xFF
} temp_monitor_error_type_t;

typedef enum
{
    TEMP_MONITOR_ERROR_SENSOR_READ = 0x00,
    TEMP_MONITOR_ERROR_SENSOR_FAULT = 0x01,
    TEMP_MONITOR_HARDWARE_FAILURE = 0x02,
    TEMP_MONITOR_ERROR_SPI_COMMUNICATION = 0x03,
    TEMP_MONITOR_ERROR_TOO_MANY_UNRESPONSIVE_SENSORS = 0x04,
} temp_monitor_hw_error_type_t;

typedef enum
{
    TEMP_MONITOR_ERROR_TOO_MANY_SAMPLES = 0x00,
    TEMP_MONITOR_ERROR_OVER_TEMP = 0x01,
} temp_monitor_data_error_type_t;

typedef enum
{
    TEMP_ERR_TYPE_NONE = 0,
    TEMP_ERR_TYPE_OVER_TEMP = (1 << 0),
    TEMP_ERR_TYPE_HW = (1 << 1),
    TEMP_ERR_TYPE_DATA = (1 << 2),
} temp_monitor_error_flags_t;

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

    // Samples collected tracking
    uint8_t samples_collected;
    uint8_t bad_samples_collected;

    furnace_error_t error_buffer[CONFIG_TEMP_SENSORS_MAX_SENSORS * 2];
    uint8_t num_errors;

    uint8_t num_hw_errors;
    uint8_t num_data_errors;
    uint8_t num_over_temp_errors;
    furnace_error_severity_t highest_error_severity;
} temp_monitor_context_t;

// Single global context pointer (internal to component)
extern temp_monitor_context_t* g_temp_monitor_ctx;

// ----------------------------
// Configuration
// ----------------------------
typedef struct
{
    const char* task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} TempMonitorConfig_t;

esp_err_t start_temperature_monitor_task(temp_monitor_context_t* ctx);

esp_err_t stop_temperature_monitor_task(temp_monitor_context_t* ctx);

esp_err_t init_temp_sensors(temp_monitor_context_t* ctx);

void read_temp_sensors_data(const temp_monitor_context_t* ctx, temp_sample_t* temp_sample_to_fill);

bool temp_ring_buffer_init();

void temp_ring_buffer_push(temp_ring_buffer_t* rb, const temp_sample_t* sample);

#endif // TEMPERATURE_MONITOR_INTERNAL_H

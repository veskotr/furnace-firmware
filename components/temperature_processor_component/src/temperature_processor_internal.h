#ifndef TEMPERATURE_PROCESSOR_INTERNAL_H
#define TEMPERATURE_PROCESSOR_INTERNAL_H

#include "temperature_monitor_types.h"
#include "esp_err.h"
#include "event_registry.h"
#include "furnace_error_types.h"

typedef enum
{
    PROCESS_TEMPERATURE_ERROR_NONE = 0,
    PROCESS_TEMPERATURE_ERROR_INVALID_SAMPLES,
    PROCESS_TEMPERATURE_ERROR_NO_VALID_SAMPLES,
    PROCESS_TEMPERATURE_ERROR_THRESHOLD_EXCEEDED,
    PROCESS_TEMPERATURE_ERROR_INVALID_DATA
} process_temperature_error_type_t;

typedef struct
{
    uint8_t first_sensor_index;
    uint8_t second_sensor_index;
    float temp_delta;
} temp_sensor_pair_t;

typedef struct
{
    uint8_t anomaly_count;
    temp_sensor_pair_t temp_sensor_pairs[CONFIG_TEMP_SENSORS_MAX_SENSORS];
} temp_anomaly_result_t;

typedef struct
{
    temp_anomaly_result_t anomaly_result;
    process_temperature_error_type_t error_type;
} process_temp_result_t;

typedef struct
{
    process_temp_result_t process_temp_result_errors[CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE];
    size_t number_of_error_results;
    process_temperature_error_type_t error_type;
} process_temp_samples_result_t;

typedef struct
{
    // Configuration
    float temperatures_buffer[CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE];

    volatile bool processor_running;

    TaskHandle_t task_handle;
} temp_processor_context_t;

extern temp_processor_context_t* g_temp_processor_ctx;

esp_err_t start_temp_processor_task(temp_processor_context_t* ctx);

esp_err_t stop_temp_processor_task(temp_processor_context_t* ctx);

process_temp_samples_result_t process_temperature_samples(temp_processor_context_t* ctx, temp_sample_t* input_samples,
                                                        size_t number_of_samples, float* output_temperature);

esp_err_t post_temp_processor_event(temp_processor_data_t data);

#endif // TEMPERATURE_PROCESSOR_INTERNAL_H

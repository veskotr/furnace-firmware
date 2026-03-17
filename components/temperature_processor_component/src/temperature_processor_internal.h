#pragma once

#include "esp_err.h"
#include "event_registry.h"
#include "furnace_error_types.h"
#include "temp_sensor_device.h"
#include "sdkconfig.h"

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
    // Configuration
    float temperatures_buffer[CONFIG_TEMP_PROCESSOR_TEMPERATURE_BUFFER_SIZE];

    temp_sensor_device_t *temp_sensor_devices[CONFIG_TEMP_SENSORS_MAX_SENSORS];

    TaskHandle_t task_handle;

    volatile uint8_t number_of_temp_sensors;

    volatile bool processor_running;

} temp_processor_context_t;

esp_err_t start_temp_processor_task(temp_processor_context_t* ctx);

esp_err_t stop_temp_processor_task(temp_processor_context_t* ctx);

esp_err_t process_temperature_samples(temp_processor_context_t* ctx, const size_t number_of_samples,
                                      float* output_temperature);

esp_err_t init_temp_processor_events(temp_processor_context_t* ctx);

esp_err_t shutdown_temp_processor_events(temp_processor_context_t* ctx);

esp_err_t post_temp_processor_event(float average_temperature);

esp_err_t post_processing_error(furnace_error_t furnace_error);
#ifndef TEMPERATURE_PROCESSOR_INTERNAL_H
#define TEMPERATURE_PROCESSOR_INTERNAL_H

#include "temperature_monitor_types.h"
#include "esp_err.h"
#include "event_registry.h"

typedef struct
{
    // Configuration
    float temperatures_buffer[CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE];

    volatile bool processor_running;

    TaskHandle_t task_handle;

} temp_processor_context_t;

extern temp_processor_context_t *g_temp_processor_ctx;

esp_err_t start_temp_processor_task(temp_processor_context_t *ctx);

esp_err_t stop_temp_processor_task(temp_processor_context_t *ctx);

process_temperature_error_t process_temperature_samples(temp_processor_context_t *ctx, temp_sample_t *input_samples, size_t number_of_samples, float *output_temperature);

esp_err_t post_temp_processor_event(process_temperature_event_t event_type, void *event_data, size_t event_data_size);

#endif // TEMPERATURE_PROCESSOR_INTERNAL_H
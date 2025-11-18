#ifndef TEMPERATURE_PROCESSOR_TYPES_H
#define TEMPERATURE_PROCESSOR_TYPES_H

#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(PROCESS_TEMPERATURE_EVENT);

typedef enum {
    PROCESS_TEMPERATURE_ERROR_NONE = 0,
    PROCESS_TEMPERATURE_ERROR_INVALID_DATA,
    PROCESS_TEMPERATURE_ERROR_COMPUTATION_FAILED,
    PROCESS_TEMPERATURE_THRESHOLD_EXCEEDED
} process_temperature_error_type_t;

typedef struct {
    process_temperature_error_type_t error_type;
    uint8_t sensor_index;
} process_temperature_error_t;

typedef struct{
    bool anomaly_detected;
    float average_temperature;
    process_temperature_error_t *errors_info;
    size_t errors_count;
} process_temperature_data_t;

typedef enum {
    PROCESS_TEMPERATURE_EVENT_DATA = 0,
    PROCESS_TEMPERATURE_EVENT_ERROR
} process_temperature_event_t;

#endif // TEMPERATURE_PROCESSOR_TYPES_H
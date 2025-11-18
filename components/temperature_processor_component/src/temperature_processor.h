#ifndef TEMPERATURE_PROCESSOR_H
#define TEMPERATURE_PROCESSOR_H

#include "temperature_processor_types.h"
#include "temperature_monitor_component.h"

// Component tag
process_temperature_error_t process_temperature_samples(temp_sample_t *input_samples, size_t number_of_samples, float *output_temperature);

#endif // TEMPERATURE_PROCESSOR_H
#ifndef TEMPERATURE_PROCESSOR_COMPONENT_H
#define TEMPERATURE_PROCESSOR_COMPONENT_H

#include "esp_err.h"
#include "esp_event.h"

esp_err_t init_temp_processor(void);

esp_err_t shutdown_temp_processor(void);

#endif
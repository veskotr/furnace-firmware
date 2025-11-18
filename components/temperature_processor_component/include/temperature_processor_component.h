#ifndef TEMPERATURE_PROCESSOR_COMPONENT_H
#define TEMPERATURE_PROCESSOR_COMPONENT_H

#include "esp_err.h"
#include "esp_event.h"

esp_err_t init_temp_processor(esp_event_loop_handle_t loop_handle);

esp_err_t shutdown_temp_processor(void);

#endif
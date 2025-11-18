#ifndef TEMPERATURE_PROCESSOR_TASK_H
#define TEMPERATURE_PROCESSOR_TASK_H

#include "esp_err.h"
#include "esp_event.h"
#include <stdbool.h>

extern volatile bool processor_running;
extern esp_event_loop_handle_t temp_processor_event_loop;

esp_err_t start_temp_processor_task(void);

esp_err_t stop_temp_processor_task(void);

#endif // TEMPERATURE_PROCESSOR_TASK_H
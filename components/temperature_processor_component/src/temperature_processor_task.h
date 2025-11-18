#ifndef TEMPERATURE_PROCESSOR_TASK_H
#define TEMPERATURE_PROCESSOR_TASK_H

#include "esp_err.h"
#include <stdbool.h>

extern volatile bool processor_running;

esp_err_t start_temp_processor_task(void);

esp_err_t stop_temp_processor_task(void);

#endif // TEMPERATURE_PROCESSOR_TASK_H
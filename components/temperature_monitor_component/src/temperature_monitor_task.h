#ifndef TEMPERATURE_MONITOR_TASK_H
#define TEMPERATURE_MONITOR_TASK_H

#include "temperature_monitor_component.h"
#include "temperature_monitor_types.h"
#include "temperature_monitor_internal_types.h"

extern bool monitor_running;

esp_err_t start_temperature_monitor_task(void);

esp_err_t stop_temperature_monitor_task(void);

#endif // TEMPERATURE_MONITOR_TASK_H
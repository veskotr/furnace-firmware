#ifndef TEMPERATURE_MONITOR_TASK_H
#define TEMPERATURE_MONITOR_TASK_H

#include "temperature_monitor_component.h"

#include "freertos/task.h"
// Task handle
extern TaskHandle_t temp_monitor_task_handle;

extern temp_sensors_array_t temp_sensors_array;

static bool monitor_running;

static void temp_monitor_task(void *args);

#endif // TEMPERATURE_MONITOR_TASK_H
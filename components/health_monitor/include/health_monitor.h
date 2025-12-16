#ifndef HEALTH_MONITOR_H
#define HEALTH_MONITOR_H

#include "esp_err.h"

esp_err_t init_health_monitor(void);
esp_err_t shutdown_health_monitor(void);

#endif // HEALTH_MONITOR_H

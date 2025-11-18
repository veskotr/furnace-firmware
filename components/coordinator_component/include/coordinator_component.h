#ifndef COORDINATOR_COMPONENT_H
#define COORDINATOR_COMPONENT_H

#include "esp_err.h"

esp_err_t init_coordinator();

esp_err_t stop_coordinator(void);

#endif // COORDINATOR_COMPONENT_H
#ifndef COORDINATOR_COMPONENT_H
#define COORDINATOR_COMPONENT_H

#include "esp_err.h"
#include "core_types.h"
#include "coordinator_component_types.h"

esp_err_t init_coordinator(const coordinator_config_t* config);

esp_err_t stop_coordinator(void);

#endif // COORDINATOR_COMPONENT_H

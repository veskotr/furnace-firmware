#ifndef COORDINATOR_COMPONENT_H
#define COORDINATOR_COMPONENT_H

#include "esp_err.h"
#include "core_types.h"

esp_err_t init_coordinator(const heating_profile_t *profiles, size_t num_profiles);

esp_err_t stop_coordinator(void);

#endif // COORDINATOR_COMPONENT_H
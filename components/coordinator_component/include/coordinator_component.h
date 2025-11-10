#ifndef COORDINATOR_COMONENT_H
#define COORDINATOR_COMONENT_H

#include "esp_err.h"
#include "esp_event.h"
#include <inttypes.h>
#include "core_types.h"

esp_err_t init_coordinator();

esp_err_t start_coordinator(heating_profile_t *profile);

esp_err_t pause_coordinator(void);

esp_err_t resume_coordinator(void);

esp_err_t stop_coordinator(void);

#endif // COORDINATOR_COMONENT_H
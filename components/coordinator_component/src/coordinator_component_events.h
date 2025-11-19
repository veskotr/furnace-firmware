#ifndef COORDINATOR_COMPONENT_EVENTS_H
#define COORDINATOR_COMPONENT_EVENTS_H
#include "esp_event.h"
#include "esp_err.h"

extern esp_event_loop_handle_t coordinator_event_loop_handle;

esp_err_t init_coordinator_events(void);

esp_err_t shutdown_coordinator_events(void);

#endif // COORDINATOR_COMPONENT_EVENTS_H
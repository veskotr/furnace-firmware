#ifndef COORDINATOR_COMPONENT_EVENTS_H
#define COORDINATOR_COMPONENT_EVENTS_H
#include "esp_event.h"
#include "esp_err.h"

extern esp_event_loop_handle_t coordinator_event_loop_handle;

esp_err_t init_coordinator_events(void);

esp_err_t shutdown_coordinator_events(void);

esp_err_t send_coordinator_error_event(coordinator_tx_event_t event_type, esp_err_t *event_data, coordinator_error_code_t coordinator_error_code);

esp_err_t send_coordinator_event(coordinator_tx_event_t event_type, void *event_data, size_t event_data_size);

#endif // COORDINATOR_COMPONENT_EVENTS_H
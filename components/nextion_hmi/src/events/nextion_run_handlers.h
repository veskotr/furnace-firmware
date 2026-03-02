#pragma once

#include <stdbool.h>

// User-initiated run commands (called from line router)
void handle_run_start(void);
void handle_run_pause(void);
void handle_run_stop(void);
void handle_confirm_end(void);
bool nextion_is_profile_running(void);

// NOTE: nextion_event_handle_temp_update, nextion_event_handle_profile_*
// are declared in nextion_events_internal.h (called by hmi_coordinator)
// and implemented in this module.

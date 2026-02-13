#pragma once

#include <stdbool.h>
#include "event_registry.h"

void nextion_event_handle_line(const char *line);
void nextion_event_handle_init(void);
void nextion_update_main_status(void);

/* ── Phase 4: event-driven handlers ──────────────────────────────── */
void nextion_event_handle_temp_update(float temperature, bool valid);
void nextion_event_handle_profile_started(void);
void nextion_event_handle_profile_paused(void);
void nextion_event_handle_profile_resumed(void);
void nextion_event_handle_profile_stopped(void);
void nextion_event_handle_profile_error(coordinator_error_code_t code,
                                        esp_err_t esp_err);

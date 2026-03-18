#pragma once

void handle_settings_init(void);
void handle_save_settings(const char *payload);
void handle_restart(void);
void handle_factory_reset_request(void);
void handle_factory_reset_confirm(void);

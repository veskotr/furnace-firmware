#pragma once

void handle_manual_toggle(void);
void handle_manual_temp_inc(void);
void handle_manual_temp_dec(void);
void handle_manual_temp_set(const char *value);
void handle_manual_delta_inc(void);
void handle_manual_delta_dec(void);
void handle_manual_delta_set(const char *value);
void handle_fan_mode_toggle(void);

/**
 * @brief Hide all manual control UI elements on the Nextion display.
 *        Called when manual mode stops or a program stops.
 */
void nextion_hide_manual_controls(void);

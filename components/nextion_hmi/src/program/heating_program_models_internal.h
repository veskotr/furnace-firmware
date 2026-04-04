#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core_types.h"

void program_models_init(void);
void program_draft_clear(void);
void program_draft_set_name(const char *name);
bool program_draft_set_stage(uint8_t stage_number,
                             int t_min,
                             int target_t_c,
                             int t_delta_min,
                             int delta_t_per_min_x10,
                             bool t_set,
                             bool target_set,
                             bool t_delta_set,
                             bool delta_t_set);
void program_draft_clear_stage(uint8_t stage_number);
void program_draft_get(program_draft_t *out);
const char *program_draft_get_name(void);
void program_set_current_temp_c(int temp_c);
int program_get_current_temp_c(void);
void program_set_current_temp_f(float temp_f);
float program_get_current_temp_f(void);
void program_set_ambient_temp_c(int temp_c);
int program_get_ambient_temp_c(void);
void program_set_current_kw(int kw);
int program_get_current_kw(void);
void hmi_get_run_program(program_draft_t *out);

// Operational time — NVS-persisted, incremented each status update
uint32_t program_get_operational_time_sec(void);
void program_add_operational_time_sec(uint32_t seconds);

// Manual mode state
bool program_get_manual_mode_active(void);
void program_set_manual_mode_active(bool active);
int  program_get_manual_target_temp_c(void);
void program_set_manual_target_temp_c(int temp_c);
int  program_get_manual_delta_t_x10(void);
void program_set_manual_delta_t_x10(int delta_x10);

// Fan mode
bool program_get_fan_mode_max(void);
void program_set_fan_mode_max(bool is_max);

// Cooldown rate (x10, NVS-persisted, overrides Kconfig default)
int  program_get_cooldown_rate_x10(void);
void program_set_cooldown_rate_x10(int rate_x10);

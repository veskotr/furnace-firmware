#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "heating_program_types.h"

// ============================================================================
// Program validation — domain-level checks
// ============================================================================
// Full-draft validation: checks all stages for range, completeness, and
// mathematical consistency. Returns true if the program is valid.
// The _with_temp variant accepts the current furnace temperature as
// the starting point for inter-stage math consistency checks.
bool program_validate_draft(const ProgramDraft *draft, char *error_msg, size_t error_len);
bool program_validate_draft_with_temp(const ProgramDraft *draft, int start_temp_c,
                                      char *error_msg, size_t error_len);

// Individual field range helpers — used by both validate_draft and autofill
bool validate_temp_in_range(int target_t_c, int stage_num, char *err, size_t err_len);
bool validate_time_in_range(int t_min, int stage_num, char *err, size_t err_len);
bool validate_delta_t_in_range(int delta_t_x10, int stage_num, char *err, size_t err_len);

// Format a x10 fixed-point value as decimal string (e.g. 15 -> "1.5")
void format_x10_value(int val_x10, char *buf, size_t buf_len);

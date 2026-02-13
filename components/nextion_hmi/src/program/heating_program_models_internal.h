#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "heating_program_models.h"

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
const ProgramDraft *program_draft_get(void);
void program_set_current_temp_c(int temp_c);
int program_get_current_temp_c(void);
void program_set_current_kw(int kw);
int program_get_current_kw(void);

// Copy the current draft into the run slot (used before starting a program)
void program_copy_draft_to_run_slot(void);

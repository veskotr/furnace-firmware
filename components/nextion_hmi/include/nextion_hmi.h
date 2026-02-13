#pragma once

#include <stddef.h>
#include "heating_program_types.h"

void nextion_hmi_init(void);

// Returns pointer to the "run slot" — a static ProgramDraft used during execution.
// The coordinator stores this pointer at init time. Before starting a program,
// the HMI copies the validated draft into this slot via program_copy_draft_to_run_slot().
const ProgramDraft *hmi_get_run_program(size_t *out_count);

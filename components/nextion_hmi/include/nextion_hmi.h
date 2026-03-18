#pragma once

#include <stddef.h>
#include "heating_program_types.h"

void nextion_hmi_init(void);

// Copy the run-slot program into caller-provided buffer (thread-safe).
void hmi_get_run_program(ProgramDraft *out);

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "core_types.h"
#include "program_models.h"

heating_profile_t *hmi_get_profile_slots(size_t *out_count);

bool hmi_build_profile_from_draft(const ProgramDraft *draft, char *error_msg, size_t error_len);

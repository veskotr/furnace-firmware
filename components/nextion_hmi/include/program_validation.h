#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "config.h"
#include "program_models.h"

bool program_validate_draft(const ProgramDraft *draft, const AppConfig *config, char *error_msg, size_t error_len);

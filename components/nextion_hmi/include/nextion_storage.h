#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "program_models.h"

bool nextion_storage_active(void);
bool nextion_storage_save_program(const ProgramDraft *draft, const char *original_name, char *error_msg, size_t error_len);
bool nextion_storage_delete_program(const char *name, char *error_msg, size_t error_len);

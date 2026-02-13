#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "heating_program_models.h"

bool nextion_storage_active(void);
bool nextion_storage_save_program(const ProgramDraft *draft, const char *original_name, char *error_msg, size_t error_len);
bool nextion_storage_delete_program(const char *name, char *error_msg, size_t error_len);
bool nextion_storage_parse_file_to_draft(const char *filename, char *error_msg, size_t error_len);

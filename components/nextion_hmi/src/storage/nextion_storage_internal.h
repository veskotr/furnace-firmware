#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "core_types.h"

bool nextion_storage_active(void);
bool nextion_storage_save_program(const program_draft_t *draft, const char *original_name, char *error_msg, size_t error_len);
bool nextion_storage_delete_program(const char *name, char *error_msg, size_t error_len);
bool nextion_storage_parse_file_to_draft(const char *filename, char *error_msg, size_t error_len);
bool nextion_serialize_program(const program_draft_t *draft, char *out, size_t out_len);

/* Program name registry — tracks programs saved/loaded this session */
void nextion_storage_register_program(const char *display_name);
int  nextion_storage_delete_all_programs(void);

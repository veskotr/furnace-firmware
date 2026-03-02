#pragma once

#include <stddef.h>
#include <stdbool.h>

bool nextion_file_reader_active(void);
bool nextion_read_file(const char *path, char *out, size_t max_len, size_t *out_len);
bool nextion_file_exists(const char *path);

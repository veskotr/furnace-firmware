#pragma once

#include <stddef.h>
#include <stdbool.h>

void nextion_file_reader_init(void);
void nextion_file_reader_feed(unsigned char byte);
bool nextion_file_reader_active(void);
bool nextion_read_file(const char *path, char *out, size_t max_len, size_t *out_len);
bool nextion_file_exists(const char *path);

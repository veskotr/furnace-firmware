#pragma once

#include <stdbool.h>
#include <stddef.h>

// Parse an integer from text. Returns false on empty or invalid input.
bool parse_int(const char *text, int *out_value);

// Parse a decimal number and return x10 fixed-point integer.
// Examples: "1.5" -> 15, "3" -> 30, "-0.5" -> -5
bool parse_decimal_x10(const char *text, int *out_value_x10);

// Format x10 value as decimal string (e.g. 15 -> "1.5")
void format_delta_x10(int val_x10, char *buf, size_t buf_len);

// Trim leading and trailing whitespace in-place. Returns trimmed pointer.
char *trim_in_place(char *text);

// Parse optional integer from text. Sets *is_set = false for empty input.
bool parse_optional_int(const char *text, int *out_value, bool *is_set);

// Parse optional delta_T (decimal or "x10=" prefix). Sets *is_set = false for empty.
bool parse_optional_delta_x10(const char *text, int *out_value_x10, bool *is_set);

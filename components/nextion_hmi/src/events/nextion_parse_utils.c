#include "nextion_parse_utils.h"

#include "heating_program_validation.h"

#include <stdlib.h>
#include <string.h>

bool parse_int(const char *text, int *out_value)
{
    if (!text || !out_value) {
        return false;
    }
    if (*text == '\0') {
        return false;
    }

    char *endptr = NULL;
    long value = strtol(text, &endptr, 10);
    if (*endptr != '\0') {
        return false;
    }

    *out_value = (int)value;
    return true;
}

bool parse_decimal_x10(const char *text, int *out_value_x10)
{
    if (!text || !out_value_x10) {
        return false;
    }
    if (*text == '\0') {
        return false;
    }

    int sign = 1;
    const char *ptr = text;
    if (*ptr == '-') {
        sign = -1;
        ptr++;
    } else if (*ptr == '+') {
        ptr++;
    }

    int whole = 0;
    while (*ptr >= '0' && *ptr <= '9') {
        whole = whole * 10 + (*ptr - '0');
        ptr++;
    }

    int frac = 0;
    if (*ptr == '.') {
        ptr++;
        if (*ptr >= '0' && *ptr <= '9') {
            frac = *ptr - '0';
            ptr++;
            while (*ptr >= '0' && *ptr <= '9') {
                ptr++;
            }
        }
    }

    if (*ptr != '\0') {
        return false;
    }

    *out_value_x10 = sign * (whole * 10 + frac);
    return true;
}

void format_delta_x10(int val_x10, char *buf, size_t buf_len)
{
    format_x10_value(val_x10, buf, buf_len);
}

char *trim_in_place(char *text)
{
    if (!text) {
        return text;
    }

    while (*text == ' ' || *text == '\t') {
        ++text;
    }

    char *end = text + strlen(text);
    while (end > text) {
        char c = *(end - 1);
        if (c != ' ' && c != '\t') {
            break;
        }
        --end;
    }
    *end = '\0';
    return text;
}

bool parse_optional_int(const char *text, int *out_value, bool *is_set)
{
    if (!is_set) {
        return false;
    }
    if (!text || text[0] == '\0') {
        *is_set = false;
        return true;
    }

    char *trimmed = trim_in_place((char *)text);
    if (trimmed[0] == '\0') {
        *is_set = false;
        return true;
    }

    int value = 0;
    if (!parse_int(trimmed, &value)) {
        return false;
    }

    *out_value = value;
    *is_set = true;
    return true;
}

bool parse_optional_delta_x10(const char *text, int *out_value_x10, bool *is_set)
{
    if (!is_set) {
        return false;
    }
    if (!text || text[0] == '\0') {
        *is_set = false;
        return true;
    }

    char *trimmed = trim_in_place((char *)text);
    if (trimmed[0] == '\0') {
        *is_set = false;
        return true;
    }

    int value_x10 = 0;
    if (strncmp(trimmed, "x10=", 4) == 0) {
        if (!parse_int(trimmed + 4, &value_x10)) {
            return false;
        }
    } else {
        if (!parse_decimal_x10(trimmed, &value_x10)) {
            return false;
        }
    }

    *out_value_x10 = value_x10;
    *is_set = true;
    return true;
}

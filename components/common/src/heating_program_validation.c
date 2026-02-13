#include "heating_program_validation.h"

#include "sdkconfig.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error_msg, size_t error_len, const char *msg)
{
    if (!error_msg || error_len == 0) {
        return;
    }
    snprintf(error_msg, error_len, "%s", msg);
}

// Helper to check if calculated value matches expected within tolerance
static bool within_tolerance_int(int calculated, int expected, int tolerance)
{
    int diff = calculated - expected;
    if (diff < 0) diff = -diff;
    return diff <= tolerance;
}

// Helper to format x10 value as decimal string (e.g., 15 -> "1.5")
void format_x10_value(int val_x10, char *buf, size_t buf_len)
{
    int whole = val_x10 / 10;
    int frac = val_x10 % 10;
    if (frac < 0) frac = -frac;
    if (val_x10 < 0 && whole == 0) {
        snprintf(buf, buf_len, "-0.%d", frac);
    } else {
        snprintf(buf, buf_len, "%d.%d", whole, frac);
    }
}

/* --- Shared validation helpers --- */

bool validate_temp_in_range(int target_t_c, int stage_num, char *err, size_t err_len)
{
    if (target_t_c > CONFIG_NEXTION_MAX_TEMPERATURE_C) {
        snprintf(err, err_len, "Stage %d: Temp exceeds max %d",
            stage_num, CONFIG_NEXTION_MAX_TEMPERATURE_C);
        return false;
    }
    if (target_t_c < 0) {
        snprintf(err, err_len, "Stage %d: Temp cannot be negative", stage_num);
        return false;
    }
    return true;
}

bool validate_time_in_range(int t_min, int stage_num, char *err, size_t err_len)
{
    if (t_min <= 0) {
        snprintf(err, err_len, "Stage %d: Time must be > 0", stage_num);
        return false;
    }
    if (t_min > CONFIG_NEXTION_MAX_OPERATIONAL_TIME_MIN) {
        snprintf(err, err_len, "Stage %d: Time exceeds max %d",
            stage_num, CONFIG_NEXTION_MAX_OPERATIONAL_TIME_MIN);
        return false;
    }
    return true;
}

bool validate_delta_t_in_range(int delta_t_x10, int stage_num, char *err, size_t err_len)
{
    if (delta_t_x10 > CONFIG_NEXTION_DELTA_T_MAX_PER_MIN_X10) {
        char buf[16];
        format_x10_value(CONFIG_NEXTION_DELTA_T_MAX_PER_MIN_X10, buf, sizeof(buf));
        snprintf(err, err_len, "Stage %d: Delta T exceeds max %s", stage_num, buf);
        return false;
    }
    if (delta_t_x10 < CONFIG_NEXTION_DELTA_T_MIN_PER_MIN_X10) {
        char buf[16];
        format_x10_value(CONFIG_NEXTION_DELTA_T_MIN_PER_MIN_X10, buf, sizeof(buf));
        snprintf(err, err_len, "Stage %d: Delta T below min %s", stage_num, buf);
        return false;
    }
    return true;
}

// Validate mathematical consistency of a single stage
// Formula: (target_temp - start_temp) * 10 = delta_t_per_min_x10 * t_min
// All delta_t values are x10 fixed-point (15 = 1.5°C/min)
// Returns true if consistent, false if not achievable
static bool validate_stage_math(
    int start_temp_c,
    int target_temp_c,
    int t_min,
    int delta_t_x10,
    int stage_num,
    char *error_msg,
    size_t error_len)
{
    // Temperature difference in whole degrees
    int temp_diff = target_temp_c - start_temp_c;
    // Temperature difference scaled x10 for fixed-point math
    int temp_diff_x10 = temp_diff * 10;

    // Special case: maintain temperature (temp_diff = 0)
    if (temp_diff == 0) {
        // delta_t should be 0 for maintaining temp
        if (delta_t_x10 != 0) {
            snprintf(error_msg, error_len,
                "Stage %d: Delta T must be 0 to maintain temp", stage_num);
            return false;
        }
        return true;  // Time can be anything for maintaining
    }

    char delta_buf[16];

    // Check delta_t bounds
    if (delta_t_x10 > CONFIG_NEXTION_DELTA_T_MAX_PER_MIN_X10) {
        int calc_time = temp_diff_x10 / CONFIG_NEXTION_DELTA_T_MAX_PER_MIN_X10;
        if (calc_time < 0) calc_time = -calc_time;
        format_x10_value(CONFIG_NEXTION_DELTA_T_MAX_PER_MIN_X10, delta_buf, sizeof(delta_buf));
        snprintf(error_msg, error_len,
            "Stage %d: Delta T max is %s. Need %d min",
            stage_num, delta_buf, calc_time);
        return false;
    }

    if (delta_t_x10 < CONFIG_NEXTION_DELTA_T_MIN_PER_MIN_X10) {
        int calc_time = temp_diff_x10 / CONFIG_NEXTION_DELTA_T_MIN_PER_MIN_X10;
        if (calc_time < 0) calc_time = -calc_time;
        format_x10_value(CONFIG_NEXTION_DELTA_T_MIN_PER_MIN_X10, delta_buf, sizeof(delta_buf));
        snprintf(error_msg, error_len,
            "Stage %d: Delta T min is %s. Need %d min",
            stage_num, delta_buf, calc_time);
        return false;
    }

    // Calculate expected final temperature based on time and delta_t
    int expected_temp_x10 = (start_temp_c * 10) + (delta_t_x10 * t_min);
    int expected_temp = expected_temp_x10 / 10;

    // Check if we reach target within tolerance
    if (!within_tolerance_int(expected_temp, target_temp_c, CONFIG_NEXTION_TEMP_TOLERANCE_C)) {
        int correct_time = (delta_t_x10 != 0) ? (temp_diff_x10 / delta_t_x10) : 0;
        if (correct_time < 0) correct_time = -correct_time;
        int correct_delta_x10 = (t_min > 0) ? (temp_diff_x10 / t_min) : 0;

        format_x10_value(correct_delta_x10, delta_buf, sizeof(delta_buf));
        snprintf(error_msg, error_len,
            "Stage %d: Won't reach %dC. Need t=%d or dT=%s",
            stage_num, target_temp_c, correct_time, delta_buf);
        return false;
    }

    // Check time deviation
    if (delta_t_x10 != 0) {
        int calc_time_min = temp_diff_x10 / delta_t_x10;
        if (calc_time_min < 0) calc_time_min = -calc_time_min;

        int time_tolerance_min = (CONFIG_NEXTION_TIME_TOLERANCE_SEC + 59) / 60;

        if (!within_tolerance_int(calc_time_min, t_min, time_tolerance_min)) {
            format_x10_value(delta_t_x10, delta_buf, sizeof(delta_buf));
            snprintf(error_msg, error_len,
                "Stage %d: Time mismatch. Need %d min at dT=%s",
                stage_num, calc_time_min, delta_buf);
            return false;
        }
    }

    return true;
}

bool program_validate_draft(const ProgramDraft *draft, char *error_msg, size_t error_len)
{
    return program_validate_draft_with_temp(draft, 0, error_msg, error_len);
}

bool program_validate_draft_with_temp(const ProgramDraft *draft, int start_temp_c,
                                      char *error_msg, size_t error_len)
{
    if (!draft) {
        set_error(error_msg, error_len, "Internal config error");
        return false;
    }

    if (draft->name[0] == '\0') {
        set_error(error_msg, error_len, "Program name required");
        return false;
    }

    bool has_non_space = false;
    for (size_t i = 0; draft->name[i] != '\0'; ++i) {
        char c = draft->name[i];
        if (c == ',') {
            set_error(error_msg, error_len, "Program name cannot contain commas");
            return false;
        }
        if (!(isalnum((unsigned char)c) || c == ' ')) {
            set_error(error_msg, error_len, "Program name must be letters/numbers");
            return false;
        }
        if (c != ' ') {
            has_non_space = true;
        }
    }

    if (!has_non_space) {
        set_error(error_msg, error_len, "Program name required");
        return false;
    }

    int total_time = 0;
    bool any_stage = false;
    int current_temp = start_temp_c;

    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        const ProgramStage *stage = &draft->stages[i];
        if (!stage->is_set) {
            continue;
        }

        any_stage = true;
        int stage_num = i + 1;

        if (!stage->t_set || !stage->target_set || !stage->delta_t_set) {
            snprintf(error_msg, error_len, "Stage %d: Incomplete fields", stage_num);
            return false;
        }

        if (!validate_time_in_range(stage->t_min, stage_num, error_msg, error_len)) {
            return false;
        }

        if (!validate_temp_in_range(stage->target_t_c, stage_num, error_msg, error_len)) {
            return false;
        }

        if (stage->t_delta_min < CONFIG_NEXTION_T_DELTA_MIN_MIN) {
            snprintf(error_msg, error_len, "Stage %d: Delta t below min %d",
                stage_num, CONFIG_NEXTION_T_DELTA_MIN_MIN);
            return false;
        }

        if (!validate_delta_t_in_range(stage->delta_t_per_min_x10, stage_num, error_msg, error_len)) {
            return false;
        }

        // Mathematical consistency check
        if (!validate_stage_math(current_temp, stage->target_t_c, stage->t_min,
                stage->delta_t_per_min_x10, stage_num, error_msg, error_len)) {
            return false;
        }

        total_time += stage->t_min;

        // Check running total against max (early exit if already exceeded)
        if (total_time > CONFIG_NEXTION_MAX_OPERATIONAL_TIME_MIN) {
            snprintf(error_msg, error_len, "Total time %d exceeds max %d at stage %d",
                total_time, CONFIG_NEXTION_MAX_OPERATIONAL_TIME_MIN, stage_num);
            return false;
        }

        current_temp = stage->target_t_c;  // Next stage starts at this temp
    }

    if (!any_stage) {
        set_error(error_msg, error_len, "At least one stage required");
        return false;
    }

    if (total_time < CONFIG_NEXTION_MIN_OPERATIONAL_TIME_MIN) {
        snprintf(error_msg, error_len, "Program time %d below min %d",
            total_time, CONFIG_NEXTION_MIN_OPERATIONAL_TIME_MIN);
        return false;
    }

    // Final total time check (redundant but explicit)
    if (total_time > CONFIG_NEXTION_MAX_OPERATIONAL_TIME_MIN) {
        snprintf(error_msg, error_len, "Program time %d exceeds max %d",
            total_time, CONFIG_NEXTION_MAX_OPERATIONAL_TIME_MIN);
        return false;
    }

    return true;
}

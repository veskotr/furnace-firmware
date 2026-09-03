#include "heating_program_graph_internal.h"
#include "heating_program_models_internal.h"

#include "sdkconfig.h"
#include <string.h>

static uint8_t clamp_u8(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return (uint8_t)value;
}

uint8_t program_graph_encode_temp(float temp_c, int max_temp_c)
{
    if (max_temp_c <= 0) {
        return 0;
    }
    if (temp_c < 0.0f) {
        return 0;
    }
    /* Compute the byte in float, round to nearest, clamp to 0..255. Doing the
     * scale in float (rather than int truncation against pixel height) is what
     * makes the projected and live curves overlap exactly. */
    const float scaled = (temp_c * 255.0f) / (float)max_temp_c;
    return clamp_u8((int)(scaled + 0.5f));
}

/* Per-stage rate-based duration in minutes.
 * Centralised here so both the projected-curve keypoint walk below and
 * program_calculate_stages_duration_ms compute the same number for every
 * stage — that's what keeps the rendered curve and the live waveform's
 * ms-per-pixel aligned on the X axis. */
static float stage_duration_minutes(const program_stage_t *stage,
                                    float prev_temp,
                                    int cooldown_rate_x10)
{
    const float target = (float)stage->target_t_c;
    float diff = target - prev_temp;
    const bool is_cooling = diff < 0.0f;
    if (is_cooling) {
        diff = -diff;
    }

    if (diff > 0.5f) {
        int rate_x10 = stage->delta_t_per_min_x10;
        if (rate_x10 == 0 && is_cooling && cooldown_rate_x10 > 0) {
            rate_x10 = cooldown_rate_x10;
        }
        if (rate_x10 > 0) {
            return (diff * 10.0f) / (float)rate_x10;
        }
    }
    /* Dwell stage, or no rate available — t_min is the time budget. */
    return (float)stage->t_min;
}

uint32_t program_calculate_stages_duration_ms(const program_draft_t *draft,
                                              int start_temp_c,
                                              int cooldown_rate_x10,
                                              float *out_last_temp)
{
    if (!draft) {
        return 0;
    }

    float total_min = 0.0f;
    float prev_temp = (float)start_temp_c;
    bool any_stage = false;

    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        const program_stage_t *stage = &draft->stages[i];
        if (!stage->is_set || !stage->t_set || !stage->target_set) {
            continue;
        }
        total_min += stage_duration_minutes(stage, prev_temp, cooldown_rate_x10);
        prev_temp = (float)stage->target_t_c;
        any_stage = true;
    }

    if (out_last_temp) {
        *out_last_temp = any_stage ? prev_temp : (float)start_temp_c;
    }
    return (uint32_t)(total_min * 60.0f * 1000.0f);
}

size_t program_build_graph(const program_draft_t *draft, uint8_t *out, size_t max_len, int width_px, int max_temp_c, int start_temp_c)
{
    if (!draft || !out || max_len == 0 || width_px <= 0 || max_temp_c <= 0) {
        return 0;
    }

    memset(out, 0, max_len);

    /*
     * Build keypoints: each active stage adds one (cumulative_time, temperature)
     * entry.  The first keypoint is the starting condition at time 0.
     */
    float kp_time[PROGRAMS_TOTAL_STAGE_COUNT + 2];  /* +2: start + cooldown */
    float kp_temp[PROGRAMS_TOTAL_STAGE_COUNT + 2];
    int   n_keys = 0;

    kp_time[0] = 0.0f;
    kp_temp[0] = (float)start_temp_c;
    n_keys = 1;

    float total_time = 0.0f;
    const int cd_rate = program_get_cooldown_rate_x10();

    /*
     * Per-stage duration is rate-driven where possible — matches what the
     * coordinator actually does at runtime, and what calculate_program_
     * duration_ms uses for the remaining-time estimate:
     *
     *   minutes = |target − previous_temp| / (rate_x10 / 10)
     *           = |target − previous_temp| × 10 / rate_x10
     *
     * For pure dwell stages (no temperature change) we use t_min directly,
     * since that IS the hold duration. For cooling-direction stages with
     * no own rate (the editor stores cooling stages with delta_t_per_min_x10
     * = 0), we fall back to the program-level cooldown rate so the graph
     * still shows a sensible slope.
     *
     * Using `start_temp_c` (the actual chamber temperature at run time) as
     * the seed means the first-stage ramp on the graph starts from where
     * the chamber really is — not from an assumed designed start temp.
     */
    float prev_temp = (float)start_temp_c;
    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        const program_stage_t *stage = &draft->stages[i];
        if (!stage->is_set || !stage->t_set || !stage->target_set) {
            continue;
        }

        total_time += stage_duration_minutes(stage, prev_temp, cd_rate);
        kp_time[n_keys] = total_time;
        kp_temp[n_keys] = (float)stage->target_t_c;
        n_keys++;

        prev_temp = (float)stage->target_t_c;
    }

    if (n_keys <= 1 || total_time <= 0.0f) {
        return 0;
    }

    /*
     * Implicit cooldown stage: ramp from last temperature to 0 C.
     * Duration is derived from the configured natural cooling rate.
     */
    float last_temp = kp_temp[n_keys - 1];
    if (last_temp > 0.0f && cd_rate > 0) {
        float cooldown_min = (last_temp * 10.0f) / (float)cd_rate;
        if (cooldown_min < 1.0f) {
            cooldown_min = 1.0f;
        }
        total_time += cooldown_min;
        kp_time[n_keys] = total_time;
        kp_temp[n_keys] = 0.0f;
        n_keys++;
    }

    /* Output count is the full available width, clamped to buffer size. */
    size_t count = (size_t)width_px;
    if (count > max_len) {
        count = max_len;
    }

    /*
     * For every output pixel, compute the time it represents and linearly
     * interpolate the temperature from the surrounding keypoints.  This
     * always stretches (or compresses) the profile to fill the full width.
     */
    for (size_t x = 0; x < count; ++x) {
        float t = (count > 1)
                  ? (total_time * (float)x / (float)(count - 1))
                  : 0.0f;

        /* Find the segment this time falls into */
        float temp_val = kp_temp[n_keys - 1]; /* default: last keypoint */
        for (int k = 1; k < n_keys; ++k) {
            if (t <= kp_time[k]) {
                float seg_len = kp_time[k] - kp_time[k - 1];
                float frac = (seg_len > 0.0f)
                             ? ((t - kp_time[k - 1]) / seg_len)
                             : 1.0f;
                temp_val = kp_temp[k - 1]
                         + (kp_temp[k] - kp_temp[k - 1]) * frac;
                break;
            }
        }

        out[x] = program_graph_encode_temp(temp_val, max_temp_c);
    }

    return count;
}

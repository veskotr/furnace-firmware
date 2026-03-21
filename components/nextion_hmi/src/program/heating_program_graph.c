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

    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        const program_stage_t *stage = &draft->stages[i];
        if (!stage->is_set || !stage->t_set || !stage->target_set) {
            continue;
        }
        total_time += (float)stage->t_min;
        kp_time[n_keys] = total_time;
        kp_temp[n_keys] = (float)stage->target_t_c;
        n_keys++;
    }

    if (n_keys <= 1 || total_time <= 0.0f) {
        return 0;
    }

    /*
     * Implicit cooldown stage: ramp from last temperature to 0 C.
     * Duration is derived from the configured natural cooling rate.
     */
    float last_temp = kp_temp[n_keys - 1];
    int cd_rate = program_get_cooldown_rate_x10();
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

        int capped = (int)temp_val;
        if (capped < 0) {
            capped = 0;
        }
        if (capped > max_temp_c) {
            capped = max_temp_c;
        }

        int mapped = (int)((float)capped * 255.0f / (float)max_temp_c);
        out[x] = clamp_u8(mapped);
    }

    return count;
}

#include "program_graph.h"

#include <math.h>

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

size_t program_build_graph(const ProgramDraft *draft, uint8_t *out, size_t max_len, int width_px, int max_temp_c, int start_temp_c)
{
    if (!draft || !out || max_len == 0 || width_px <= 0 || max_temp_c <= 0) {
        return 0;
    }

    int current_temp = start_temp_c;
    size_t total_points = 0;

    // count total points needed
    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        const ProgramStage *stage = &draft->stages[i];
        if (!stage->is_set || !stage->t_set || !stage->target_set) {
            continue;
        }

        int steps = stage->t_delta_min > 0 ? (stage->t_min / stage->t_delta_min) : stage->t_min;
        if (steps <= 0) {
            steps = 1;
        }
        total_points += (size_t)steps;
        current_temp = stage->target_t_c;
    }

    if (total_points == 0) {
        return 0;
    }

    float scale = (total_points > (size_t)width_px) ? ((float)total_points / (float)width_px) : 1.0f;

    current_temp = start_temp_c;
    size_t out_count = 0;
    size_t point_index = 0;

    // plot points
    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        const ProgramStage *stage = &draft->stages[i];
        if (!stage->is_set || !stage->t_set || !stage->target_set) {
            continue;
        }

        int steps = stage->t_delta_min > 0 ? (stage->t_min / stage->t_delta_min) : stage->t_min;
        if (steps <= 0) {
            steps = 1;
        }

        for (int s = 1; s <= steps; ++s) {
            float progress = (float)s / (float)steps;
            float temp = (float)current_temp + (float)(stage->target_t_c - current_temp) * progress;
            int capped_temp = (int)temp;
            if (capped_temp < 0) {
                capped_temp = 0;
            }
            if (capped_temp > max_temp_c) {
                capped_temp = max_temp_c;
            }

            size_t bucket = (size_t)((float)point_index / scale);
            if (bucket >= (size_t)width_px) {
                bucket = (size_t)width_px - 1;
            }
            if (bucket >= max_len) {
                return out_count;
            }

            if (bucket >= out_count) {
                out_count = bucket + 1;
            }

            int mapped = (int)((float)capped_temp * 255.0f / (float)max_temp_c);
            out[bucket] = clamp_u8(mapped);
            point_index++;
        }

        current_temp = stage->target_t_c;
    }

    return out_count;
}

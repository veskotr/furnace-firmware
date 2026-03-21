#pragma once

#include <stdbool.h>

typedef struct
{
    uint32_t profile_index;
    float current_temperature;
    float target_temperature;
    bool is_active;
    bool is_paused;
    bool is_completed;
    uint32_t current_time_elapsed_ms;
    uint32_t estimated_total_duration_ms;   // Estimated total program duration (stages + cooldown)
    uint32_t heating_stages_duration_ms;     // Duration of heating stages only (excl. cooldown)
    bool heating_element_on;
    bool fan_on;
} heating_task_state_t;


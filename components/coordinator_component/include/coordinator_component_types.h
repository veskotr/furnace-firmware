#ifndef COORDINATOR_COMPONENT_TYPES_H
#define COORDINATOR_COMPONENT_TYPES_H

#include <stdbool.h>
#include <inttypes.h>

typedef struct
{
    uint32_t profile_index;
    float current_temperature;
    float target_temperature;
    bool is_active;
    bool is_paused;
    bool is_completed;
    uint32_t current_time_elapsed_ms;
    uint32_t total_time_ms;
    bool heating_element_on;
    bool fan_on;
} heating_task_state_t;

typedef struct
{
    heating_profile_t *profiles;
    size_t num_profiles;
} coordinator_config_t;

#endif // COORDINATOR_COMPONENT_TYPES_H
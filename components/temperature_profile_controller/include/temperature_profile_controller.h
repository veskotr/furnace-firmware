#ifndef TEMPERATURE_PROFILE_COMPONENT_H
#define TEMPERATURE_PROFILE_COMPONENT_H

#include "core_types.h"

typedef enum
{
    PROFILE_CONTROLLER_ERROR_NONE = 0,
    PROFILE_CONTROLLER_ERROR_INVALID_ARG,
    PROFILE_CONTROLLER_ERROR_NO_PROFILE_LOADED,
    PROFILE_CONTROLLER_ERROR_COMPUTATION_FAILED,
    PROFILE_CONTROLLER_ERROR_TIME_EXCEEDS_PROFILE_DURATION,
    PROFILE_CONTROLLER_ERROR_UNKNOWN

} profile_controller_error_t;

profile_controller_error_t load_heating_profile(heating_profile_t *heating_profile, float starting_temperature);

profile_controller_error_t get_target_temperature_at_time(uint32_t time_ms, float *temperature);

profile_controller_error_t shutdown_profile_controller(void);

#endif // TEMPERATURE_PROFILE_COMPONENT_H
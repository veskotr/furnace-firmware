#ifndef TEMPERATURE_PROFILE_TYPES_H
#define TEMPERATURE_PROFILE_TYPES_H
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

typedef struct {
    float initial_temperature;
    heating_profile_t *heating_profile;
} temp_profile_config_t;

#endif // TEMPERATURE_PROFILE_TYPES_H
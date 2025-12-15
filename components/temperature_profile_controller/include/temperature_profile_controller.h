#ifndef TEMPERATURE_PROFILE_COMPONENT_H
#define TEMPERATURE_PROFILE_COMPONENT_H

#include "temperature_profile_types.h"

profile_controller_error_t load_heating_profile(const temp_profile_config_t config);

profile_controller_error_t get_target_temperature_at_time(const uint32_t time_ms, float *temperature);

profile_controller_error_t shutdown_profile_controller(void);

#endif // TEMPERATURE_PROFILE_COMPONENT_H
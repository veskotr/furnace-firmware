#include "temperature_profile_controller.h"
#include "logger_component.h"
#include <math.h>

static const char *TAG = "TEMP_PROFILE_CONTROLLER";

static heating_profile_t *current_profile = NULL;

static float initial_temperature = 0.0f;

profile_controller_error_t load_heating_profile(heating_profile_t *heating_profile, float starting_temperature)
{
    if (heating_profile == NULL || heating_profile->first_node == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid heating profile");
        return PROFILE_CONTROLLER_ERROR_INVALID_ARG;
    }

    current_profile = heating_profile;

    initial_temperature = starting_temperature;

    return PROFILE_CONTROLLER_ERROR_NONE;
}

profile_controller_error_t get_target_temperature_at_time(uint32_t time_ms, float *temperature)
{
    if (current_profile == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Heating profile not loaded or invalid argument");
        return PROFILE_CONTROLLER_ERROR_NO_PROFILE_LOADED;
    }

    heating_node_t *node = current_profile->first_node;
    uint32_t elapsed_time = 0;
    float start_temp = initial_temperature;

    while (node != NULL)
    {
        if (time_ms <= elapsed_time + node->duration_ms)
        {
            // Calculate target temperature based on node type
            float t = (float)(time_ms - elapsed_time) / (float)node->duration_ms;
            switch (node->type)
            {
            case NODE_TYPE_LOG:
                *temperature = start_temp + (node->set_temp - start_temp) * log10f(1 + 9 * t);
                break;
            case NODE_TYPE_LINEAR:
                *temperature = start_temp + (node->set_temp - start_temp) * t;
                break;
            case NODE_TYPE_SQUARE:
                *temperature = start_temp + (node->set_temp - start_temp) * t * t;
                break;
            case NODE_TYPE_CUBE:
                *temperature = start_temp + (node->set_temp - start_temp) * t * t * t;
                break;
            default:
                LOGGER_LOG_ERROR(TAG, "Unknown node type");
                return PROFILE_CONTROLLER_ERROR_INVALID_ARG;
            }
            return PROFILE_CONTROLLER_ERROR_NONE;
        }
        elapsed_time += node->duration_ms;
        start_temp = node->set_temp;
        node = node->next_node;
    }

    return PROFILE_CONTROLLER_ERROR_TIME_EXCEEDS_PROFILE_DURATION;
}

profile_controller_error_t shutdown_profile_controller(void)
{
    current_profile = NULL;
    initial_temperature = 0.0f;

    return PROFILE_CONTROLLER_ERROR_NONE;
}

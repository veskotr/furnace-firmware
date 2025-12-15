#include "temperature_profile_controller.h"
#include "logger_component.h"
#include <math.h>

static const char *TAG = "TEMP_PROFILE_CONTROLLER";
typedef struct {
    float initial_temperature;
    heating_profile_t *heating_profile;
} temperature_profile_controller_context_t;

static temperature_profile_controller_context_t *g_temp_profile_controller_ctx = NULL;

profile_controller_error_t load_heating_profile(const temp_profile_config_t config)
{
    if (config.heating_profile == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid heating profile");
        return PROFILE_CONTROLLER_ERROR_INVALID_ARG;
    }

    if (g_temp_profile_controller_ctx == NULL)
    {
        g_temp_profile_controller_ctx = calloc(1, sizeof(temperature_profile_controller_context_t));
        if (g_temp_profile_controller_ctx == NULL)
        {
            LOGGER_LOG_ERROR(TAG, "Failed to allocate temperature profile controller context");
            return PROFILE_CONTROLLER_ERROR_UNKNOWN;
        }
    }

    g_temp_profile_controller_ctx->heating_profile = config.heating_profile;

    g_temp_profile_controller_ctx->initial_temperature = config.initial_temperature;

    return PROFILE_CONTROLLER_ERROR_NONE;
}

profile_controller_error_t get_target_temperature_at_time(const uint32_t time_ms, float *temperature)
{
    if (g_temp_profile_controller_ctx == NULL || g_temp_profile_controller_ctx->heating_profile == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Heating profile not loaded or invalid argument");
        return PROFILE_CONTROLLER_ERROR_NO_PROFILE_LOADED;
    }

    heating_node_t *node = g_temp_profile_controller_ctx->heating_profile->first_node;
    uint32_t elapsed_time = 0;
    float start_temp = g_temp_profile_controller_ctx->initial_temperature;

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
    if (g_temp_profile_controller_ctx == NULL)
    {
        return PROFILE_CONTROLLER_ERROR_NONE;
    }

    free(g_temp_profile_controller_ctx);
    g_temp_profile_controller_ctx = NULL;

    return PROFILE_CONTROLLER_ERROR_NONE;
}

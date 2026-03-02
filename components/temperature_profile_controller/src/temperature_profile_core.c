#include "temperature_profile_controller.h"
#include "sdkconfig.h"
#include "logger_component.h"
#include <math.h>

static const char *TAG = "TEMP_PROFILE_CONTROLLER";

typedef struct {
    float initial_temperature;
    const ProgramDraft *program;    // Points to the ProgramDraft being executed
} temperature_profile_controller_context_t;

static temperature_profile_controller_context_t *g_temp_profile_controller_ctx = NULL;

profile_controller_error_t load_heating_profile(const temp_profile_config_t config)
{
    if (config.program == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid program (NULL)");
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

    g_temp_profile_controller_ctx->program = config.program;
    g_temp_profile_controller_ctx->initial_temperature = config.initial_temperature;

    return PROFILE_CONTROLLER_ERROR_NONE;
}

profile_controller_error_t get_target_temperature_at_time(const uint32_t time_ms, float *temperature)
{
    if (g_temp_profile_controller_ctx == NULL || g_temp_profile_controller_ctx->program == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Program not loaded or invalid argument");
        return PROFILE_CONTROLLER_ERROR_NO_PROFILE_LOADED;
    }

    const ProgramDraft *prog = g_temp_profile_controller_ctx->program;
    uint32_t elapsed_time = 0;
    float start_temp = g_temp_profile_controller_ctx->initial_temperature;

    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i)
    {
        const ProgramStage *stage = &prog->stages[i];
        if (!stage->is_set)
        {
            continue;
        }

        uint32_t stage_duration_ms = (uint32_t)stage->t_min * 60U * 1000U;

        if (time_ms <= elapsed_time + stage_duration_ms)
        {
            // Linear interpolation within this stage
            float t = (float)(time_ms - elapsed_time) / (float)stage_duration_ms;
            *temperature = start_temp + ((float)stage->target_t_c - start_temp) * t;
            return PROFILE_CONTROLLER_ERROR_NONE;
        }

        elapsed_time += stage_duration_ms;
        start_temp = (float)stage->target_t_c;
    }

    /* Implicit cooldown: ramp from last stage temp to 0 C */
    if (start_temp > 0.0f && CONFIG_NEXTION_COOLDOWN_RATE_X10 > 0) {
        float cooldown_min = (start_temp * 10.0f) / (float)CONFIG_NEXTION_COOLDOWN_RATE_X10;
        if (cooldown_min < 1.0f) {
            cooldown_min = 1.0f;
        }
        uint32_t cooldown_ms = (uint32_t)(cooldown_min * 60.0f * 1000.0f);

        if (time_ms <= elapsed_time + cooldown_ms) {
            float t = (float)(time_ms - elapsed_time) / (float)cooldown_ms;
            *temperature = start_temp * (1.0f - t);
            return PROFILE_CONTROLLER_ERROR_NONE;
        }
        elapsed_time += cooldown_ms;
    }

    /* Past cooldown — hold at 0 */
    *temperature = 0.0f;
    return PROFILE_CONTROLLER_ERROR_NONE;
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

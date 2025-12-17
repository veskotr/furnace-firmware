#include "coordinator_component.h"
#include "logger_component.h"
#include "coordinator_component_internal.h"
#include "utils.h"
#include "sdkconfig.h"

static const char *TAG = "COORDINATOR_CORE";

static coordinator_ctx_t *g_coordinator_ctx;

esp_err_t init_coordinator(const coordinator_config_t *config)
{
    if (g_coordinator_ctx != NULL && g_coordinator_ctx->running)
    {
        return ESP_OK;
    }
    // Allocate context if needed
    if (g_coordinator_ctx == NULL)
    {
        g_coordinator_ctx = calloc(1, sizeof(coordinator_ctx_t));
        if (g_coordinator_ctx == NULL)
        {
            LOGGER_LOG_ERROR(TAG, "Failed to allocate coordinator context");
            return ESP_ERR_NO_MEM;
        }
    }

    g_coordinator_ctx->heating_profiles = (heating_profile_t *)config->profiles;
    g_coordinator_ctx->num_profiles = config->num_profiles;

    // Initialize Coordinator Events
    CHECK_ERR_LOG_RET(init_coordinator_events(g_coordinator_ctx),
                      "Failed to initialize coordinator events");

    return ESP_OK;
}

esp_err_t coordinator_list_heating_profiles(void)
{
    if (g_coordinator_ctx->heating_profiles == NULL || g_coordinator_ctx->num_profiles == 0)
    {
        LOGGER_LOG_WARN(TAG, "No heating profiles available");
        return ESP_ERR_NOT_FOUND;
    }

    LOGGER_LOG_INFO(TAG, "Available Heating Profiles:");
    for (size_t i = 0; i < g_coordinator_ctx->num_profiles; i++)
    {
        const heating_profile_t *profile = &g_coordinator_ctx->heating_profiles[i];
        LOGGER_LOG_INFO(TAG, "Profile Index: %d, Name: %s, Duration: %d ms, Target Temp: %.2f C",
                        i,
                        profile->name);
    }
    return ESP_OK;
}

esp_err_t stop_coordinator(void)
{
    if (g_coordinator_ctx == NULL || !g_coordinator_ctx->running)
    {
        return ESP_OK;
    }

    CHECK_ERR_LOG_RET(shutdown_coordinator_events(g_coordinator_ctx),
                      "Failed to shutdown coordinator events");

    CHECK_ERR_LOG_RET(stop_heating_profile(g_coordinator_ctx),
                      "Failed to stop heating profile");

    free(g_coordinator_ctx);
    g_coordinator_ctx = NULL;

    return ESP_OK;
}

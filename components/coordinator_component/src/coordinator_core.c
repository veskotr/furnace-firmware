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

    g_coordinator_ctx->programs = config->programs;
    g_coordinator_ctx->num_programs = config->num_programs;

    // Initialize Coordinator Events
    CHECK_ERR_LOG_RET(init_coordinator_events(g_coordinator_ctx),
                      "Failed to initialize coordinator events");

    return ESP_OK;
}

esp_err_t coordinator_list_heating_profiles(void)
{
    if (g_coordinator_ctx->programs == NULL || g_coordinator_ctx->num_programs == 0)
    {
        LOGGER_LOG_WARN(TAG, "No programs available");
        return ESP_ERR_NOT_FOUND;
    }

    LOGGER_LOG_INFO(TAG, "Available Programs:");
    for (size_t i = 0; i < g_coordinator_ctx->num_programs; i++)
    {
        const ProgramDraft *prog = &g_coordinator_ctx->programs[i];
        LOGGER_LOG_INFO(TAG, "Program Index: %d, Name: %s", i, prog->name);
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

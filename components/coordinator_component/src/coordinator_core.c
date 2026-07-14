#include "coordinator_component.h"
#include "logger_component.h"
#include "coordinator_component_internal.h"
#include "utils.h"
#include "sdkconfig.h"
#include "heater_controller_component.h"

static const char* TAG = "COORDINATOR_CORE";

coordinator_ctx_t* g_coordinator_ctx;

esp_err_t init_coordinator(void)
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

    g_coordinator_ctx->has_program = false;
    g_coordinator_ctx->temperature_mutex = xSemaphoreCreateMutex();
    if (g_coordinator_ctx->temperature_mutex == NULL)
    {
        free(g_coordinator_ctx);
        g_coordinator_ctx = NULL;
        return ESP_ERR_NO_MEM;
    }

    g_coordinator_ctx->target_update_mutex = xSemaphoreCreateMutex();
    if (g_coordinator_ctx->target_update_mutex == NULL)
    {
        vSemaphoreDelete(g_coordinator_ctx->temperature_mutex);
        free(g_coordinator_ctx);
        g_coordinator_ctx = NULL;
        return ESP_ERR_NO_MEM;
    }

    g_coordinator_ctx->exit_semaphore = xSemaphoreCreateBinary();
    if (g_coordinator_ctx->exit_semaphore == NULL)
    {
        vSemaphoreDelete(g_coordinator_ctx->target_update_mutex);
        vSemaphoreDelete(g_coordinator_ctx->temperature_mutex);
        free(g_coordinator_ctx);
        g_coordinator_ctx = NULL;
        return ESP_ERR_NO_MEM;
    }
    atomic_init(&g_coordinator_ctx->sensor_data_inhibited, true);
    CHECK_ERR_LOG_RET(heater_controller_set_sensor_data_inhibit(true),
                      "Failed to establish startup sensor-data inhibit");

    // Initialize Coordinator Events
    CHECK_ERR_LOG_RET(init_coordinator_events(g_coordinator_ctx),
                      "Failed to initialize coordinator events");

    return ESP_OK;
}

esp_err_t coordinator_list_heating_profiles(void)
{
    if (!g_coordinator_ctx->has_program)
    {
        LOGGER_LOG_WARN(TAG, "No programs available");
        return ESP_ERR_NOT_FOUND;
    }

    LOGGER_LOG_INFO(TAG, "Available Programs:");
    const program_draft_t *prog = &g_coordinator_ctx->run_program;
    LOGGER_LOG_INFO(TAG, "Program Index: 0, Name: %s", prog->name);
    return ESP_OK;
}

esp_err_t stop_coordinator(void)
{
    if (g_coordinator_ctx == NULL)
    {
        return ESP_OK;
    }

    CHECK_ERR_LOG_RET(shutdown_coordinator_events(g_coordinator_ctx),
                      "Failed to shutdown coordinator events");

    CHECK_ERR_LOG_RET(stop_heating_profile(g_coordinator_ctx),
                      "Failed to stop heating profile");

    if (g_coordinator_ctx->temperature_mutex != NULL)
    {
        vSemaphoreDelete(g_coordinator_ctx->temperature_mutex);
        g_coordinator_ctx->temperature_mutex = NULL;
    }
    if (g_coordinator_ctx->target_update_mutex != NULL)
    {
        vSemaphoreDelete(g_coordinator_ctx->target_update_mutex);
        g_coordinator_ctx->target_update_mutex = NULL;
    }
    if (g_coordinator_ctx->exit_semaphore != NULL)
    {
        vSemaphoreDelete(g_coordinator_ctx->exit_semaphore);
        g_coordinator_ctx->exit_semaphore = NULL;
    }
    free(g_coordinator_ctx);
    g_coordinator_ctx = NULL;

    return ESP_OK;
}

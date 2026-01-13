#include "health_monitor.h"
#include "health_monitor_internal.h"
#include "event_registry.h"
#include "utils.h"
#include <stdlib.h>

static const char* TAG = "HEALTH_MONITOR_CORE";

static health_monitor_ctx_t* g_health_monitor_ctx;

void init_heartbeats(health_monitor_ctx_t* ctx);

esp_err_t init_health_monitor(void)
{
    // Initialization code for health monitor
    if (g_health_monitor_ctx != NULL && g_health_monitor_ctx->initialized)
    {
        return ESP_OK;
    }
    g_health_monitor_ctx = calloc(1, sizeof(health_monitor_ctx_t));
    if (g_health_monitor_ctx == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to allocate health monitor context");
        return ESP_ERR_NO_MEM;
    }

    CHECK_ERR_LOG_RET(init_health_monitor_events(g_health_monitor_ctx),
                      "Failed to initialize health monitor events"
    );

    CHECK_ERR_LOG_RET(init_health_monitor_task(g_health_monitor_ctx),
                      "Failed to initialize health monitor task"
    );

    init_heartbeats(g_health_monitor_ctx);

    g_health_monitor_ctx->initialized = true;
    return ESP_OK;
}

esp_err_t shutdown_health_monitor(void)
{
    // Shutdown code for health monitor
    if (g_health_monitor_ctx == NULL || !g_health_monitor_ctx->initialized)
    {
        return ESP_OK;
    }

    CHECK_ERR_LOG_RET(shutdown_health_monitor_events(g_health_monitor_ctx),
                      "Failed to shutdown health monitor events");

    CHECK_ERR_LOG_RET(shutdown_health_monitor_task(g_health_monitor_ctx),
                      "Failed to shutdown health monitor task");

    free(g_health_monitor_ctx);
    g_health_monitor_ctx = NULL;

    return ESP_OK;
}

void init_heartbeats(health_monitor_ctx_t* ctx)
{
    const TickType_t now = xTaskGetTickCount();

    // Init heartbeats for temp monitor
    ctx->heartbeat[TEMP_MONITOR_EVENT_HEARTBEAT] = (heartbeat_entry_t){
        .last_seen_tick = now,
        .max_silence_ticks = pdMS_TO_TICKS(CONFIG_HEALTH_MONITOR_TEMP_MONITOR_MAX_SILENCE_MS),

        .max_misses = CONFIG_HEALTH_MONITOR_TEMP_MONITOR_MAX_MISSES,
        .miss_count = 0,

        .required = true,
        .state = HB_STATE_OK
    };

    // Init heartbeats for temp processor
    ctx->heartbeat[TEMP_PROCESSOR_EVENT_HEARTBEAT] = (heartbeat_entry_t){
        .last_seen_tick = now,
        .max_silence_ticks = pdMS_TO_TICKS(CONFIG_HEALTH_MONITOR_TEMP_PROCESSOR_MAX_SILENCE_MS),

        .max_misses = CONFIG_HEALTH_MONITOR_TEMP_PROCESSOR_MAX_MISSES,
        .miss_count = 0,

        .required = true,
        .state = HB_STATE_OK
    };

    // Init heartbeats for heater controller
    ctx->heartbeat[HEATER_CONTROLLER_EVENT_HEARTBEAT] = (heartbeat_entry_t){
        .last_seen_tick = now,
        .max_silence_ticks = pdMS_TO_TICKS(CONFIG_HEALTH_MONITOR_HEATER_CONTROLLER_MAX_SILENCE_MS),

        .max_misses = CONFIG_HEALTH_MONITOR_HEATER_CONTROLLER_MAX_MISSES,
        .miss_count = 0,

        .required = true,
        .state = HB_STATE_OK
    };

    // Init heartbeats for coordinator
    ctx->heartbeat[COORDINATOR_EVENT_HEARTBEAT] = (heartbeat_entry_t){
        .last_seen_tick = now,
        .max_silence_ticks = pdMS_TO_TICKS(CONFIG_HEALTH_MONITOR_COORDINATOR_MAX_SILENCE_MS),

        .max_misses = CONFIG_HEALTH_MONITOR_COORDINATOR_MAX_MISSES,
        .miss_count = 0,

        .required = true,
        .state = HB_STATE_OK
    };
}

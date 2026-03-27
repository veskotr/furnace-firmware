#include "health_monitor_internal.h"
#include "event_manager.h"
#include "event_registry.h"
#include "logger_component.h"
#include "utils.h"

static const char *TAG = "HEALTH_MONITOR_EVENTS";

static void init_heartbeats(health_monitor_ctx_t *ctx, const health_monitor_data_t *component_data);
static void unregister_heartbeats(health_monitor_ctx_t *ctx, const health_monitor_data_t *component_data);

void health_monitor_event_handler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data)
{
    health_monitor_ctx_t *ctx = (health_monitor_ctx_t *)handler_arg;
    const health_monitor_data_t *data = (health_monitor_data_t *)event_data;
    if (data == NULL)
    {
        LOGGER_LOG_WARN(TAG, "Received heartbeat event with NULL data");
        return;
    }
    // TODO: warning for always false, is it correct?
    if (data->component_id < 0 || data->component_id >= CONFIG_HEARTH_BEAT_COUNT)
    {
        LOGGER_LOG_WARN(TAG, "Received heartbeat event for invalid component ID: %d", data->component_id);
        return;
    }
    switch (id)
    {
    case HEALTH_MONITOR_EVENT_HEARTBEAT:
    {
        ctx->heartbeat[data->component_id].last_seen_tick = xTaskGetTickCount();
        ctx->heartbeat[data->component_id].max_silence_ticks = data->timeout_ticks;
        LOGGER_LOG_INFO(TAG, "[%d:%s] Heartbeat, timeout: %d", data->component_id, data->component_name,
                        data->timeout_ticks);
        break;
    }
    case HEALTH_MONITOR_EVENT_REGISTER:
    {
        init_heartbeats(ctx, data);
        LOGGER_LOG_INFO(TAG, "[%d:%s] Heartbeat, timeout: %d", data->component_id, data->component_name,
                        data->timeout_ticks);
        break;
    }
    case HEALTH_MONITOR_EVENT_UNREGISTER:
    {
        unregister_heartbeats(ctx, data);
        LOGGER_LOG_INFO(TAG, "[%d:%s] Unregistered from health monitor", data->component_id, data->component_name);
        break;
    }
    default:
        LOGGER_LOG_WARN(TAG, "Unknown Health Monitor Event ID: %d", id);
        break;
    }
}

esp_err_t init_health_monitor_events(health_monitor_ctx_t *ctx)
{
    event_manager_subscribe(
        HEALTH_MONITOR_EVENT,
        ESP_EVENT_ANY_ID,
        &health_monitor_event_handler,
        ctx);
    LOGGER_LOG_INFO(TAG, "Health monitor events initialized");
    ctx->events_initialized = true;

    return ESP_OK;
}

esp_err_t shutdown_health_monitor_events(health_monitor_ctx_t *ctx)
{
    event_manager_unsubscribe(
        HEALTH_MONITOR_EVENT,
        ESP_EVENT_ANY_ID,
        &health_monitor_event_handler);
    LOGGER_LOG_INFO(TAG, "Health monitor events shut down");
    ctx->events_initialized = false;

    return ESP_OK;
}

static void init_heartbeats(health_monitor_ctx_t *ctx, const health_monitor_data_t *component_data)
{
    const TickType_t now = xTaskGetTickCount();

    ctx->heartbeat[component_data->component_id] = (heartbeat_entry_t){
        .last_seen_tick = now,
        .max_silence_ticks = component_data->timeout_ticks,

        .max_misses = CONFIG_HEALTH_MONITOR_TEMP_MONITOR_MAX_MISSES,
        .miss_count = 0,

        .registered = true,
        .state = HB_STATE_OK};
}

static void unregister_heartbeats(health_monitor_ctx_t *ctx, const health_monitor_data_t *component_data)
{
    ctx->heartbeat[component_data->component_id].registered = false;
}

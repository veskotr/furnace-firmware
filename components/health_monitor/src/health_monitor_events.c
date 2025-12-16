#include "health_monitor_internal.h"
#include "event_manager.h"
#include "event_registry.h"
#include "logger_component.h"
#include "utils.h"

static const char* TAG = "HEALTH_MONITOR_EVENTS";

void health_monitor_event_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    health_monitor_ctx_t* ctx = (health_monitor_ctx_t*)handler_arg;
    switch (id)
    {
    case HEALTH_MONITOR_EVENT_HEARTBEAT:
        {
            health_monitor_component_id_t* component_id = (health_monitor_component_id_t*)event_data;
            if (*component_id < 0 || *component_id >= CONFIG_HEARTH_BEAT_COUNT)
            {
                LOGGER_LOG_WARN(TAG, "Received heartbeat for invalid component ID: %d", *component_id);
                return;
            }
            ctx->heartbeat[*component_id].last_seen_tick = xTaskGetTickCount();
            LOGGER_LOG_DEBUG(TAG, "Received heartbeat from component ID: %d", *component_id);
            break;
        }
    default:
        LOGGER_LOG_WARN(TAG, "Unknown Health Monitor Event ID: %d", id);
        break;
    }
}

esp_err_t init_health_monitor_events(health_monitor_ctx_t* ctx)
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

esp_err_t shutdown_health_monitor_events(health_monitor_ctx_t* ctx)
{
    event_manager_unsubscribe(
        HEALTH_MONITOR_EVENT,
        ESP_EVENT_ANY_ID,
        &health_monitor_event_handler);
    LOGGER_LOG_INFO(TAG, "Health monitor events shut down");
    ctx->events_initialized = false;

    return ESP_OK;
}

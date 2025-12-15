#include "event_manager.h"
#include "logger_component.h"
#include "utils.h"
#include <stdbool.h>
#include "sdkconfig.h"

static const char *TAG = "EVENT_MANAGER";

typedef struct
{
    esp_event_loop_handle_t event_loop_handle;
    bool is_initialized;
} event_manager_context_t;

typedef struct
{
    const char *task_name;
    const uint32_t stack_size;
    const UBaseType_t task_priority;
} event_manager_config_t;

static const event_manager_config_t event_manager_config = {
    .task_name = CONFIG_EVENT_MANAGER_TASK_NAME,
    .stack_size = CONFIG_EVENT_MANAGER_TASK_STACK_SIZE,
    .task_priority = CONFIG_EVENT_MANAGER_TASK_PRIORITY};

static event_manager_context_t g_event_manager_ctx = {
    .event_loop_handle = NULL,
    .is_initialized = false};

esp_err_t event_manager_init(void)
{
    if (g_event_manager_ctx.is_initialized)
    {
        return ESP_OK;
    }
    esp_event_loop_args_t loop_args = {
        .queue_size = CONFIG_EVENT_MANAGER_QUEUE_SIZE,
        .task_name = event_manager_config.task_name};

    CHECK_ERR_LOG_RET(esp_event_loop_create(&loop_args, &g_event_manager_ctx.event_loop_handle),
                      "Failed to create event manager event loop");
    g_event_manager_ctx.is_initialized = true;

    LOGGER_LOG_INFO(TAG, "Event manager initialized");
    return ESP_OK;
}

esp_err_t event_manager_shutdown(void)
{
    if (!g_event_manager_ctx.is_initialized)
    {
        return ESP_OK;
    }

    CHECK_ERR_LOG_RET(esp_event_loop_delete(g_event_manager_ctx.event_loop_handle),
                      "Failed to delete event manager event loop");
    g_event_manager_ctx.event_loop_handle = NULL;

    g_event_manager_ctx.is_initialized = false;

    LOGGER_LOG_INFO(TAG, "Event manager shutdown");
    return ESP_OK;
}

esp_err_t event_manager_subscribe(
    esp_event_base_t event_base,
    int32_t event_id,
    esp_event_handler_t handler,
    void *handler_arg)
{
    CHECK_ERR_LOG_RET(esp_event_handler_register_with(
                          g_event_manager_ctx.event_loop_handle,
                          event_base,
                          event_id,
                          handler,
                          handler_arg),
                      "Failed to subscribe to event");

    LOGGER_LOG_INFO(TAG, "Subscribed to event base %p, ID %d", event_base, event_id);
    return ESP_OK;
}

esp_err_t event_manager_unsubscribe(
    esp_event_base_t event_base,
    int32_t event_id,
    esp_event_handler_t handler)
{
    CHECK_ERR_LOG_RET(esp_event_handler_unregister_with(
                          g_event_manager_ctx.event_loop_handle,
                          event_base,
                          event_id,
                          handler),
                      "Failed to unsubscribe from event");

    LOGGER_LOG_INFO(TAG, "Unsubscribed from event base %p, ID %d", event_base, event_id);
    return ESP_OK;
}

esp_err_t event_manager_post(
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data,
    size_t event_data_size,
    TickType_t ticks_to_wait)
{
    CHECK_ERR_LOG_RET(esp_event_post_to(
                          g_event_manager_ctx.event_loop_handle,
                          event_base,
                          event_id,
                          event_data,
                          event_data_size,
                          ticks_to_wait),
                      "Failed to post event");

    LOGGER_LOG_DEBUG(TAG, "Posted event base %p, ID %d", event_base, event_id);
    return ESP_OK;
}

esp_err_t event_manager_post_immediate(
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data,
    size_t event_data_size)
{
    return event_manager_post(
        event_base,
        event_id,
        event_data,
        event_data_size,
        0);
}

esp_err_t event_manager_post_blocking(
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data,
    size_t event_data_size)
{
    return event_manager_post(
        event_base,
        event_id,
        event_data,
        event_data_size,
        portMAX_DELAY);
}

#include "temperature_processor_internal.h"
#include "utils.h"
#include "event_manager.h"
#include "event_registry.h"

static const char* TAG = "TEMP_PROCESSOR_EVENTS";

static void device_manager_event_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    temp_processor_context_t* ctx = (temp_processor_context_t*)handler_arg;
    if (base == DEVICE_MANAGER_UPDATED_EVENT)
    {
        xTaskNotifyGive(ctx->task_handle);
        LOGGER_LOG_INFO(TAG, "Device manager updated event received");
    }

    LOGGER_LOG_INFO(TAG, "Received device manager event: base=%s, id=%d", base, id);
}

esp_err_t post_temp_processor_event(float average_temperature)
{
    CHECK_ERR_LOG_RET(event_manager_post_blocking(
                          TEMP_PROCESSOR_EVENT,
                          PROCESS_TEMPERATURE_EVENT_DATA,
                          &average_temperature,
                          sizeof(float)),
                      "Failed to post temperature processor event");

    return ESP_OK;
}

esp_err_t post_processing_error(furnace_error_t furnace_error)
{
    CHECK_ERR_LOG_RET(event_manager_post_blocking(FURNACE_ERROR_EVENT,
                          FURNACE_ERROR_EVENT_ID,
                          &furnace_error,
                          sizeof(furnace_error_t)),
                      "Failed to post temperature processing error event");

    return ESP_OK;
}

esp_err_t init_temp_processor_events(temp_processor_context_t* ctx)
{
    event_manager_subscribe(DEVICE_MANAGER_UPDATED_EVENT, ESP_EVENT_ANY_ID, device_manager_event_handler, ctx);
    return ESP_OK;
}

esp_err_t shutdown_temp_processor_events(temp_processor_context_t* ctx)
{
    event_manager_unsubscribe(DEVICE_MANAGER_UPDATED_EVENT, ESP_EVENT_ANY_ID, device_manager_event_handler);
    return ESP_OK;
}

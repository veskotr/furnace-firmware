//
// Created by vesko on 9.3.2026 г..
//

#include "utils.h"
#include "device_manager_internal.h"
#include "logger_component.h"
#include "sdkconfig.h"
#include "event_manager.h"
#include "furnace_error_types.h"

static const char* TAG = "DEVICE_MANAGER_TASK";

static esp_err_t post_device_manager_event(uint16_t event_id, void* event_data, size_t event_data_size);
static esp_err_t post_furnace_error(furnace_error_t furnace_error);

static health_monitor_data_t health_monitor_data = {
    .component_id = CONFIG_DEVICE_MANAGER_COMPONENT_ID,
    .component_name = "Device Manager",
    .timeout_ticks = pdMS_TO_TICKS(CONFIG_DEVICE_MANAGER_HEARTBEAT_TIMEOUT_MS)
};

static void device_manager_task(void* args)
{
    device_manager_context_t* ctx = (device_manager_context_t*)args;

    LOGGER_LOG_INFO(TAG, "Device manager task started");

    const TickType_t period = pdMS_TO_TICKS(CONFIG_DEVICE_MANAGER_UPDATE_INTERVAL_MS);

    TickType_t next_wake_time = xTaskGetTickCount();

    while (ctx->running)
    {
        next_wake_time += period;

        for (uint8_t i = 0; i < CONFIG_DEVICE_MANAGER_MAX_DEVICES; i++)
        {
            const device_t* device = &(ctx->devices[i]);
            if (device->state != DEVICE_STATE_RUNNING)
            {
                continue;
            }
            if (device->ops == NULL || device->ops->update == NULL)
            {
                LOGGER_LOG_WARN(TAG, "Invalid device or device operations for device at index %d", i);
                continue;
            }

            const esp_err_t err = device->ops->update(device->ctx);
            if (err != ESP_OK)
            {
                LOGGER_LOG_ERROR(TAG, "Failed to update device %s (ID: %d)", device->name, device->id);
                //TODO add critical error end device failed threshold
                post_furnace_error((furnace_error_t){
                    .source = SOURCE_DEVICE_MANAGER,
                    .severity = SEVERITY_WARNING,
                    .error_code = device->id,
                });
            }
            LOGGER_LOG_INFO(TAG, "Device manager tick: updated device %s (ID: %d)", device->name, device->id);
        }
        LOGGER_LOG_INFO(TAG, "Device manager tick complete, posting update event and heartbeat");
        post_device_manager_event(DEVICE_MANAGER_UPDATED_EVENT, NULL, 0);
        event_manager_post_health(HEALTH_MONITOR_EVENT_HEARTBEAT, &health_monitor_data);

        const TickType_t now = xTaskGetTickCount();

        if (now < next_wake_time)
        {
            const TickType_t wait_ticks = next_wake_time - now;
            ulTaskNotifyTake(pdTRUE, wait_ticks);
        }
        else
            next_wake_time = now;
    }
    LOGGER_LOG_INFO(TAG, "Device manager task stopping");
    ctx->task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t init_device_manager_task(device_manager_context_t* ctx)
{
    if (ctx->running)
    {
        return ESP_OK;
    }

    ctx->running = true;

    CHECK_ERR_LOG_CALL_RET(xTaskCreate(
                               device_manager_task,
                               CONFIG_DEVICE_MANAGER_TASK_NAME,
                               CONFIG_DEVICE_MANAGER_TASK_STACK_SIZE,
                               ctx,
                               CONFIG_DEVICE_MANAGER_TASK_PRIORITY,
                               &ctx->task_handle) == pdPASS
                           ? ESP_OK
                           : ESP_FAIL,
                           ctx->running = false,
                           "Failed to create device manager task");


    return ESP_OK;
}

esp_err_t stop_device_manager_task(device_manager_context_t* ctx)
{
    if (!ctx->running)
    {
        return ESP_OK;
    }

    ctx->running = false;
    if (ctx->task_handle != NULL)
    {
        xTaskNotifyGive(ctx->task_handle);
    }

    return ESP_OK;
}

static esp_err_t post_device_manager_event(const uint16_t event_id, void* event_data, size_t event_data_size)
{
    CHECK_ERR_LOG_RET(event_manager_post_blocking(
                          DEVICE_MANAGER_EVENT,
                          event_id,
                          event_data,
                          event_data_size),
                      "Failed to post device manager event");

    return ESP_OK;
}

static esp_err_t post_furnace_error(furnace_error_t furnace_error)
{
    CHECK_ERR_LOG_RET(event_manager_post_blocking(FURNACE_ERROR_EVENT,
                          FURNACE_ERROR_EVENT_ID,
                          &furnace_error,
                          sizeof(furnace_error_t)),
                      "Failed to post device manager error event");

    return ESP_OK;
}

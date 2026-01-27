#include "commands_dispatcher_internal.h"
#include "core_types.h"
#include "logger_component.h"
#include "sdkconfig.h"
#include "utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "COMMANDS_DISPATCHER_TASK";

static const task_config_t commands_dispatcher_task_config = {
    .task_name = CONFIG_COMMANDS_DISPATCHER_TASK_NAME,
    .stack_size = CONFIG_COMMANDS_DISPATCHER_TASK_STACK_SIZE,
    .task_priority = CONFIG_COMMANDS_DISPATCHER_TASK_PRIORITY,
};


static void commands_dispatcher_task(void* args)
{
    LOGGER_LOG_INFO(TAG, "Commands Dispatcher task started");

    const commands_dispatcher_ctx_t* ctx = (commands_dispatcher_ctx_t*)args;
    command_t received_command;

    while (ctx->dispatcher_running)
    {
        // Wait for a command to be available in the queue
        if (xQueueReceive(ctx->command_queue, &received_command, portMAX_DELAY) == pdTRUE)
        {
            LOGGER_LOG_DEBUG(TAG, "Received command for target: %d", received_command.target);

            // Dispatch command to the appropriate handler
            if (received_command.target < CONFIG_COMMANDS_DISPATCHER_MAX_HANDLERS)
            {
                const handler_entry_t* handler_entry = &ctx->command_handlers[received_command.target];
                if (handler_entry->registered && handler_entry->handler != NULL)
                {
                    const esp_err_t err = handler_entry->handler(
                        received_command.data,
                        received_command.data_size);
                    if (err != ESP_OK)
                    {
                        LOGGER_LOG_ERROR(TAG, "Command handler for target %d failed with error: %d",
                                         received_command.target, err);
                    }
                }
                else
                {
                    LOGGER_LOG_WARN(TAG, "No registered handler for command target: %d", received_command.target);
                }
            }
            else
            {
                LOGGER_LOG_ERROR(TAG, "Invalid command target: %d", received_command.target);
            }
        }
    }

    LOGGER_LOG_INFO(TAG, "Commands Dispatcher task stopping");
    vTaskDelete(NULL);
}

esp_err_t init_task(commands_dispatcher_ctx_t* ctx)
{
    if (ctx->dispatcher_task_handle)
    {
        return ESP_OK;
    }

    CHECK_ERR_LOG_CALL_RET(xTaskCreate(
                               commands_dispatcher_task,
                               commands_dispatcher_task_config.task_name,
                               commands_dispatcher_task_config.stack_size,
                               ctx,
                               commands_dispatcher_task_config.task_priority,
                               &ctx->dispatcher_task_handle) == pdPASS
                           ? ESP_OK
                           : ESP_FAIL,
                           ctx->dispatcher_task_handle = NULL,
                           "Failed to create Commands Dispatcher task");

    LOGGER_LOG_INFO(TAG, "Commands Dispatcher task initialized");
    return ESP_OK;
}

esp_err_t shutdown_task(commands_dispatcher_ctx_t* ctx)
{
    if (ctx->dispatcher_task_handle == NULL)
    {
        return ESP_OK;
    }

    ctx->dispatcher_running = false;

    // Wait for the task to exit
    const TickType_t wait_ticks = pdMS_TO_TICKS(1000);
    const TickType_t start_tick = xTaskGetTickCount();
    while (ctx->dispatcher_task_handle != NULL)
    {
        if ((xTaskGetTickCount() - start_tick) > wait_ticks)
        {
            LOGGER_LOG_ERROR(TAG, "Timeout waiting for Commands Dispatcher task to stop");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    LOGGER_LOG_INFO(TAG, "Commands Dispatcher task shutdown complete");
    return ESP_OK;
}

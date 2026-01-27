#include "commands_dispatcher_internal.h"
#include "logger_component.h"
#include "sdkconfig.h"

static const char* TAG = "COMMANDS_DISPATCHER";

commands_dispatcher_ctx_t* commands_dispatcher_ctx = NULL;

esp_err_t commands_dispatcher_init(void)
{
    // Initialize any resources needed for command dispatching
    if (commands_dispatcher_ctx && commands_dispatcher_ctx->dispatcher_running)
    {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_OK;

    // Allocate context if needed
    if (commands_dispatcher_ctx == NULL)
    {
        commands_dispatcher_ctx = calloc(1, sizeof(commands_dispatcher_ctx_t));
        if (commands_dispatcher_ctx == NULL)
        {
            LOGGER_LOG_ERROR(TAG, "Failed to allocate coordinator context");
            err = ESP_ERR_NO_MEM;
            goto fail;
        }
    }

    if (commands_dispatcher_ctx->command_queue == NULL)
    {
        commands_dispatcher_ctx->command_queue = xQueueCreate(
            CONFIG_COMMANDS_DISPATCHER_QUEUE_SIZE,
            sizeof(command_t));
        if (commands_dispatcher_ctx->command_queue == NULL)
        {
            LOGGER_LOG_ERROR(TAG, "Failed to create commands dispatcher command queue");
            err = ESP_FAIL;
            goto fail;
        }
    }

    err = init_command_handlers(commands_dispatcher_ctx);
    if (err != ESP_OK)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to initialize command handlers");
        goto fail;
    }

    commands_dispatcher_ctx->dispatcher_running = true;
    err = init_task(commands_dispatcher_ctx);
    if (err != ESP_OK)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to initialize command handlers");
        goto fail;
    }
    LOGGER_LOG_INFO(TAG, "Commands Dispatcher initialized");
    return ESP_OK;

fail:
    commands_dispatcher_shutdown();
    return err;
}


esp_err_t commands_dispatcher_shutdown(void)
{
    if (!commands_dispatcher_ctx)
    {
        return ESP_OK;
    }

    esp_err_t err = ESP_OK;

    if (commands_dispatcher_ctx->dispatcher_running)
    {
        if (shutdown_task(commands_dispatcher_ctx) != ESP_OK)
        {
            LOGGER_LOG_ERROR(TAG, "Failed to shutdown dispatcher task");
            err = ESP_FAIL;
        }
    }

    if (shutdown_command_handlers(commands_dispatcher_ctx) != ESP_OK)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to shutdown command handlers");
        err = ESP_FAIL;
    }

    if (commands_dispatcher_ctx->command_queue)
    {
        vQueueDelete(commands_dispatcher_ctx->command_queue);
        commands_dispatcher_ctx->command_queue = NULL;
    }

    free(commands_dispatcher_ctx);
    commands_dispatcher_ctx = NULL;

    LOGGER_LOG_INFO(TAG, "Commands Dispatcher shutdown");
    return err;
}

//
// Created by vesko on 13.1.2026 г..
//
#include "commands_dispatcher_internal.h"
#include "logger_component.h"

static const char* TAG = "COMMANDS_DISPATCHER_HANDLERS";


esp_err_t init_command_handlers(commands_dispatcher_ctx_t* ctx)
{
    // Initialize default command handlers here if needed


    LOGGER_LOG_INFO(TAG, "Command handlers initialized");
    return ESP_OK;
}

esp_err_t shutdown_command_handlers(commands_dispatcher_ctx_t* ctx)
{
    // Cleanup command handlers if needed
    if (commands_dispatcher_ctx == NULL || ctx == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid context for shutting down command handlers");
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < CONFIG_COMMANDS_DISPATCHER_MAX_HANDLERS; i++)
    {
        ctx->command_handlers[i] = (handler_entry_t){
            .handler = NULL,
            .handler_arg = NULL,
            .registered = false
        };
    }

    LOGGER_LOG_INFO(TAG, "Command handlers shutdown");
    return ESP_OK;
}

// ==============================================
// Register Command Handler
// ==============================================
esp_err_t register_command_handler(
    const command_target_t target,
    const command_handler_t handler,
    void* handler_arg
)
{
    if (commands_dispatcher_ctx == NULL || handler == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid arguments to register command handler");
        return ESP_ERR_INVALID_ARG;
    }

    if (target >= CONFIG_COMMANDS_DISPATCHER_MAX_HANDLERS)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid command target: %d", target);
        return ESP_ERR_INVALID_ARG;
    }

    if (commands_dispatcher_ctx->command_handlers[target].registered)
    {
        LOGGER_LOG_ERROR(TAG, "Command handler for target %d is already registered, overwriting", target);
        return ESP_ERR_INVALID_STATE;
    }

    commands_dispatcher_ctx->command_handlers[target] = (handler_entry_t){
        .handler = handler,
        .handler_arg = handler_arg,
        .registered = true
    };
    LOGGER_LOG_INFO(TAG, "Registered command handler for target: %d", target);
    return ESP_OK;
}

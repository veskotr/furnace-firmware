//
// Created by vesko on 13.1.2026 г..
//

#ifndef COMMANDS_DISPATCHER_INTERNAL_H
#define COMMANDS_DISPATCHER_INTERNAL_H
#include "commands_dispatcher.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    command_handler_t handler;
    void *handler_arg;
    bool registered;
} handler_entry_t;

typedef struct
{
    QueueHandle_t command_queue;
    TaskHandle_t dispatcher_task_handle;
    volatile bool dispatcher_running;

    handler_entry_t command_handlers[CONFIG_COMMANDS_DISPATCHER_MAX_HANDLERS];
} commands_dispatcher_ctx_t;

extern commands_dispatcher_ctx_t* commands_dispatcher_ctx;

// ----------------------------
// Task management
// ----------------------------
esp_err_t init_task(commands_dispatcher_ctx_t* ctx);
esp_err_t shutdown_task(commands_dispatcher_ctx_t* ctx);

// ----------------------------
// Command Handlers
// ----------------------------
esp_err_t init_command_handlers(commands_dispatcher_ctx_t* ctx);
esp_err_t shutdown_command_handlers(commands_dispatcher_ctx_t* ctx);

#endif //FURNACE_FIRMWARE_COMMANDS_DISPATCHER_INTERNAL_H
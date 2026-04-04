#pragma once

#include "esp_console.h"
#include "esp_err.h"

typedef struct
{
    char* command;
    char* help;
    esp_console_cmd_t esp_cmd;
} console_command_def_t;

esp_err_t debug_console_init(void);

esp_err_t debug_console_register_command(const console_command_def_t* cmd);
esp_err_t debug_console_unregister_command(const char* cmd_name);


esp_err_t debug_console_shutdown(void);

#include <stdio.h>
#include <string.h>

#include "debug_console.h"

#include "esp_console.h"
#include "esp_log.h"

#include "linenoise/linenoise.h"

#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"

static const char* TAG = "debug_console";

static TaskHandle_t debug_console_task_handle = NULL;
static bool debug_console_running = false;


static console_command_def_t command_helps[CONFIG_DEBUG_CONSOLE_MAX_REGISTERED_COMMANDS] = {0};
static uint8_t registered_command_count = 0;

/* =========================
 * UART INIT
 * ========================= */
static void console_uart_init(void)
{
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    uart_vfs_dev_use_driver(UART_NUM_0);
}

/* =========================
 * COMMANDS
 * ========================= */
static int cmd_help(int argc, char** argv)
{
    printf("Available commands:\n");
    printf("  help              - Show this help\n");
    for (uint8_t i = 0; i < registered_command_count; i++)
    {
        printf("  %-16s - %s\n", command_helps[i].command, command_helps[i].help);
    }
    return 0;
}

/* =========================
 * REGISTER/UNREGISTER COMMANDS
 * ========================= */
esp_err_t debug_console_register_command(const console_command_def_t* cmd)
{
    if (registered_command_count >= CONFIG_DEBUG_CONSOLE_MAX_REGISTERED_COMMANDS)
    {
        ESP_LOGE(TAG, "Cannot register command '%s': max command limit reached", cmd->command);
        return ESP_ERR_NO_MEM;
    }
    memcpy(&command_helps[registered_command_count], cmd, sizeof(console_command_def_t));
    registered_command_count++;

    const esp_err_t ret = esp_console_cmd_register(&cmd->esp_cmd);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register command '%s': %s", cmd->command, esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

esp_err_t debug_console_unregister_command(const char* cmd_name)
{
    for (uint8_t i = 0; i < registered_command_count; i++)
    {
        if (strcmp(command_helps[i].command, cmd_name) == 0)
        {
            // Shift remaining commands down
            for (uint8_t j = i; j < registered_command_count - 1; j++)
            {
                command_helps[j] = command_helps[j + 1];
            }
            memset(&command_helps[registered_command_count - 1], 0, sizeof(console_command_def_t));
            registered_command_count--;
            break;
        }
    }

    const esp_err_t ret = esp_console_cmd_deregister(cmd_name);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to unregister command '%s': %s", cmd_name, esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

/* =========================
 * CONSOLE TASK
 * ========================= */
static void debug_console_task(void* args)
{
    ESP_LOGI(TAG, "Debug console task started");

    linenoiseSetMultiLine(1);
    linenoiseHistorySetMaxLen(50);

    while (debug_console_running)
    {
        char* line = linenoise("furnace> ");

        if (line == NULL)
        {
            continue;
        }

        if (strlen(line) > 0)
        {
            linenoiseHistoryAdd(line);
        }

        int ret;
        const esp_err_t err = esp_console_run(line, &ret);

        if (err == ESP_ERR_NOT_FOUND)
        {
            printf("Command not found\n");
        }
        else if (err != ESP_OK)
        {
            printf("Error: %s\n", esp_err_to_name(err));
        }

        linenoiseFree(line);
    }

    ESP_LOGI(TAG, "Debug console task stopped");
    vTaskDelete(NULL);
}

/* =========================
 * INIT / SHUTDOWN
 * ========================= */
esp_err_t debug_console_init(void)
{
    console_uart_init();

    const esp_console_config_t console_config = {
        .max_cmdline_args = 8,
        .max_cmdline_length = CONFIG_DEBUG_CONSOLE_MAX_COMMAND_LENGTH,
    };

    esp_err_t ret = esp_console_init(&console_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize console: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_console_cmd_t help_cmd = {
        .command = "help",
        .help = "Show help",
        .hint = NULL,
        .func = &cmd_help,
    };

    ret = esp_console_cmd_register(&help_cmd);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register help command: %s", esp_err_to_name(ret));
        esp_console_deinit();
        return ret;
    }

    debug_console_running = true;

    const BaseType_t res = xTaskCreate(
        debug_console_task,
        CONFIG_DEBUG_CONSOLE_TASK_NAME,
        CONFIG_DEBUG_CONSOLE_TASK_STACK_SIZE,
        NULL,
        CONFIG_DEBUG_CONSOLE_TASK_PRIORITY,
        &debug_console_task_handle);

    if (res != pdPASS)
    {
        debug_console_shutdown();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Debug console initialized");
    return ESP_OK;
}

esp_err_t debug_console_shutdown(void)
{
    if (!debug_console_running)
    {
        return ESP_OK;
    }

    debug_console_running = false;

    esp_console_deinit();

    ESP_LOGI(TAG, "Debug console shutdown");
    return ESP_OK;
}

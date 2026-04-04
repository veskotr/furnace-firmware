//
// Created by vesko on 4.4.2026 г..
//
#include "logger_internal.h"
#include "debug_console.h"
#include "esp_log.h"

static const char* TAG = "LOGGER_CLI";
static const char* CMD_LOG_LS = "log-ls";
static const char* CMD_LOG_FILE = "log-file";

static int log_ls_cmd_func(int argc, char** argv)
{
    const esp_err_t err = list_crash_logs();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to list crash logs: %s", esp_err_to_name(err));
        return -1;
    }
    return 0;
}

static int log_file_cmd_func(int argc, char** argv)
{
    if (argc != 2)
    {
        printf("Usage: %s <filename>\n", CMD_LOG_FILE);
        return -1;
    }

    const char* filename = argv[1];
    const esp_err_t err = read_log((char*)filename);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read log file '%s': %s", filename, esp_err_to_name(err));
        return -1;
    }
    return 0;
}

esp_err_t logger_init_cli(void)
{
    const esp_console_cmd_t logger_ls_cmd = {
        .command = CMD_LOG_LS,
        .help = "Logger component commands",
        .hint = NULL,
        .func = log_ls_cmd_func,
    };

    const console_command_def_t log_cmd = {
        .command = CMD_LOG_LS,
        .help = "List crash logs stored in flash",
        .esp_cmd = logger_ls_cmd
    };

    esp_err_t ret = debug_console_register_command(&log_cmd);
    if (ret != ESP_OK)
    {
        ESP_LOGE("LOGGER_CLI", "Failed to register logger CLI command: %s", esp_err_to_name(ret));
        return ret;
    }

    const esp_console_cmd_t logger_file_cmd = {
        .command = CMD_LOG_FILE,
        .help = "Show contents of a crash log file",
        .hint = "<filename>",
        .func = log_file_cmd_func,
    };

    const console_command_def_t log_file_cmd_def = {
        .command = CMD_LOG_FILE,
        .help = "Show contents of a crash log file",
        .esp_cmd = logger_file_cmd
    };

    ret = debug_console_register_command(&log_file_cmd_def);
    if (ret != ESP_OK)
    {
        ESP_LOGE("LOGGER_CLI", "Failed to register logger file CLI command: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

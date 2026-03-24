//
// Created by vesko on 22.3.2026 г..
//
#include "nvs_flash.h"
#include "esp_log.h"
#include "logger_component.h"
#include "logger_internal.h"
#include "freertos/semphr.h"

#define CONFIG_LOG_MAX_TAG_LENGTH     16

#define LOGGER_MAGIC 0xDEADBEEF
#define LOGGER_VERSION 1

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t entry_count;
    uint8_t wrapped;
    uint16_t write_index;
} logger_header_t;

typedef struct
{
    uint32_t timestamp; // ms since boot
    uint8_t level; // log_level_t
    char tag[CONFIG_LOG_MAX_TAG_LENGTH];
    char message[CONFIG_LOG_MAX_MESSAGE_LENGTH];
} log_entry_t;

typedef struct
{
    logger_header_t header;
    log_entry_t entries[CONFIG_LOG_MAX_RING_BUFFER_SIZE];
} logger_storage_t;

static log_entry_t log_buffer[CONFIG_LOG_MAX_RING_BUFFER_SIZE];

static size_t log_index = 0;
static bool log_wrapped = false;

static const char* TAG = "LOGGER_STORAGE";

static logger_storage_t storage = {0};

static const char* level_to_string(const uint8_t level);

void store_log_entry(const log_level_t level, const char* tag, const char* message)
{
    xSemaphoreTake(log_mutex, portMAX_DELAY);
    log_entry_t* entry = &log_buffer[log_index];

    entry->timestamp = esp_log_timestamp();
    entry->level = level;

    strncpy(entry->tag, tag, CONFIG_LOG_MAX_TAG_LENGTH - 1);
    entry->tag[CONFIG_LOG_MAX_TAG_LENGTH - 1] = '\0';

    strncpy(entry->message, message, CONFIG_LOG_MAX_MESSAGE_LENGTH - 1);
    entry->message[CONFIG_LOG_MAX_MESSAGE_LENGTH - 1] = '\0';

    log_index = (log_index + 1) % CONFIG_LOG_MAX_RING_BUFFER_SIZE;

    if (log_index == 0)
    {
        log_wrapped = true;
    }
    xSemaphoreGive(log_mutex);
}

void logger_store_log_buffer(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("logger", NVS_READWRITE, &nvs);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }

    storage.header.magic = LOGGER_MAGIC;
    storage.header.version = LOGGER_VERSION;
    storage.header.entry_count = log_wrapped ? CONFIG_LOG_MAX_RING_BUFFER_SIZE : log_index;
    storage.header.wrapped = log_wrapped;
    storage.header.write_index = log_index;

    xSemaphoreTake(log_mutex, portMAX_DELAY);
    memcpy(storage.entries, log_buffer, sizeof(log_buffer));
    xSemaphoreGive(log_mutex);

    err = nvs_set_blob(nvs, "log_blob", &storage, sizeof(storage));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to write blob: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return;
    }

    err = nvs_commit(nvs);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Commit failed");
    }

    nvs_close(nvs);
}

void logger_iterate_from_nvs(const logger_output_fn_t output_fn)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("logger", NVS_READONLY, &nvs);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return;
    }

    size_t size = sizeof(storage);

    err = nvs_get_blob(nvs, "log_blob", &storage, &size);
    nvs_close(nvs);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read log blob: %s", esp_err_to_name(err));
        return;
    }

    // Validate
    if (storage.header.magic != LOGGER_MAGIC ||
        storage.header.version != LOGGER_VERSION)
    {
        ESP_LOGE(TAG, "Invalid log data: %s", esp_err_to_name(err));
        return;
    }

    const size_t count = storage.header.entry_count;

    // Oldest element:
    // - If wrapped → oldest is at write_index
    // - If not wrapped → oldest is at 0
    const size_t start = storage.header.wrapped ? storage.header.write_index : 0;

    for (size_t i = 0; i < count; i++)
    {
        const size_t idx = (start + i) % CONFIG_LOG_MAX_RING_BUFFER_SIZE;
        log_entry_t* entry = &storage.entries[idx];

        char line[CONFIG_LOG_MAX_MESSAGE_LENGTH * 2]; // Enough for tag + message

        snprintf(line, sizeof(line),
                 "%lu [%s] [%s] %s",
                 entry->timestamp,
                 level_to_string(entry->level),
                 entry->tag,
                 entry->message);

        output_fn(line);
    }
}

static void log_print_fn(const char* line)
{
    ESP_LOGI("LOG_DUMP", "%s", line);
}

void logger_dump_from_nvs(void)
{
    logger_iterate_from_nvs(log_print_fn);
}

static const char* level_to_string(const uint8_t level)
{
    switch (level)
    {
    case LOG_LEVEL_INFO: return "INFO";
    case LOG_LEVEL_WARN: return "WARN";
    case LOG_LEVEL_ERROR: return "ERROR";
    case LOG_LEVEL_DEBUG: return "DEBUG";
    case LOG_LEVEL_VERBOSE: return "VERBOSE";
    default: return "UNK";
    }
}

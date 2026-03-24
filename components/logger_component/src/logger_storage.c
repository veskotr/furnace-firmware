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

    uint32_t crash_count;
    uint32_t last_timestamp;
    uint32_t last_total_runtime;
    uint32_t error_code;
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

static void init_littlefs(void);

static bool file_exists(const char *path);

SemaphoreHandle_t log_mutex;

esp_err_t logger_init_storage(void){
    log_mutex = xSemaphoreCreateMutex();
    if (log_mutex == NULL)
    {
        ESP_LOGE(TAG, "%s", "Failed to create logger mutex");
        return ESP_ERR_NO_MEM;
    }
    return init_littlefs();
}

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

static void init_littlefs(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS (%s)", esp_err_to_name(ret));
        return;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info("storage", &total, &used);

    ESP_LOGI(TAG, "LittleFS: total=%d, used=%d", total, used);
}

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

static void store_crash_log(uint32_t error_code, logger_storage_t *ram_log)
{
    char path[64];
    snprintf(path, sizeof(path), "/littlefs/err_%lu.bin", error_code);

    logger_storage_t storage = {0};

    if (file_exists(path)) {
        FILE *f = fopen(path, "rb");
        fread(&storage, sizeof(storage), 1, f);
        fclose(f);

        // Validate
        if (storage.header.magic != 0xDEADBEEF) {
            ESP_LOGW("LOGGER", "Corrupted file, resetting");
            memset(&storage, 0, sizeof(storage));
        }
    }

    storage.header.magic = 0xDEADBEEF;
    storage.header.version = 1;
    storage.header.crash_count += 1;
    storage.header.last_timestamp = esp_log_timestamp();
    storage.header.error_code = error_code;

    memcpy(storage.entries, ram_log->entries, sizeof(storage.entries));

    storage.header.entry_count = ram_log->header.entry_count;
    storage.header.write_index = ram_log->header.write_index;
    storage.header.wrapped = ram_log->header.wrapped;

    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE("LOGGER", "Failed to open file for writing");
        return;
    }

    fwrite(&storage, sizeof(storage), 1, f);
    fclose(f);

    ESP_LOGI("LOGGER", "Crash log stored for error %lu (count=%lu)",
             error_code, storage.header.crash_count);
}
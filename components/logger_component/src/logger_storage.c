//
// Created by vesko on 22.3.2026 г..
//
#include "esp_log.h"
#include "logger_component.h"
#include "logger_internal.h"
#include "freertos/semphr.h"
#include "esp_littlefs.h"

#define CONFIG_LOG_MAX_TAG_LENGTH 16

#define LOGGER_MAGIC 0xDEADBEEF
#define LOGGER_VERSION 1

#define PANIC_MAGIC 0xBADC0FFE

typedef struct
{
    uint32_t timestamp; // ms since boot
    uint8_t level; // log_level_t
    char tag[CONFIG_LOG_MAX_TAG_LENGTH];
    char message[CONFIG_LOG_MAX_MESSAGE_LENGTH];
} log_entry_t;

typedef struct
{
    uint32_t magic;
    uint32_t error_code;
    uint32_t reset_reason;
    uint32_t timestamp;

    uint16_t write_index;

    log_entry_t entries[CONFIG_LOG_PANIC_BUFFER_COUNT];
} panic_snapshot_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint8_t wrapped;
    uint16_t write_index;

    uint32_t times_occured;
    uint32_t last_timestamp;
    uint32_t last_total_runtime;
    uint32_t error_code;
} logger_header_t;

static const char* TAG = "LOGGER_STORAGE";

SemaphoreHandle_t log_mutex;

static log_entry_t log_buffer[CONFIG_LOG_MAX_RING_BUFFER_SIZE];

static size_t log_index = 0;
static bool log_wrapped = false;

RTC_DATA_ATTR static panic_snapshot_t g_panic_snapshot;
IRAM_ATTR void panic_capture(void);

// ================================================
// Helper deffinitions
// ================================================
static const char* level_to_string(const uint8_t level);
static esp_err_t init_littlefs(void);
static esp_err_t recover_from_panic(void);
static esp_err_t store_full_crash_log(uint32_t error_cause, log_entry_t* input_buffer, uint16_t index, uint8_t wrapped);

esp_err_t logger_init_storage(void)
{
    log_mutex = xSemaphoreCreateMutex();
    if (log_mutex == NULL)
    {
        ESP_LOGE(TAG, "%s", "Failed to create logger mutex");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = init_littlefs();
    if (ret != ESP_OK)
    {
        return ret;
    }
    ret = esp_register_shutdown_handler(panic_capture);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register shutdown handler: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = recover_from_panic();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to recover from panic: %s", esp_err_to_name(ret));
        return ret;
    }

    return ret;
}

void store_full_log(uint32_t error_cause)
{
    store_full_crash_log(error_cause, log_buffer, log_index, log_wrapped);
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

static const char* level_to_string(const uint8_t level)
{
    switch (level)
    {
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_WARN:
        return "WARN";
    case LOG_LEVEL_ERROR:
        return "ERROR";
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    case LOG_LEVEL_VERBOSE:
        return "VERBOSE";
    default:
        return "UNK";
    }
}

static esp_err_t init_littlefs(void)
{
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    const esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount LittleFS (%s)", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info("storage", &total, &used);

    ESP_LOGI(TAG, "LittleFS: total=%d, used=%d", total, used);
    return ret;
}

static esp_err_t store_full_crash_log(uint32_t error_cause, log_entry_t* input_buffer, uint16_t index, uint8_t wrapped)
{
    char path[64] = {0};

    snprintf(path, sizeof(path),
             "/littlefs/err_%lu.bin", error_cause);

    FILE* f = fopen(path, "rb+");

    const bool exists = (f != NULL);

    logger_header_t header = {};

    // =========================
    // CASE A: FILE EXISTS
    // =========================
    if (exists)
    {
        // read ONLY header
        size_t r = fread(&header,
                         sizeof(logger_header_t),
                         1,
                         f);

        if (r != 1 || header.magic != LOGGER_MAGIC)
        {
            // corrupted → reset header
            memset(&header, 0, sizeof(header));
        }

        header.times_occured++;
        header.error_code = error_cause;

        // move to beginning for overwrite
        rewind(f);
        return ESP_OK;
    }
    else
    {
        // =========================
        // CASE B: NEW FILE
        // =========================
        f = fopen(path, "wb");
        if (!f)
        {
            ESP_LOGE("LOGGER", "Failed to create file");
            return ESP_FAIL;
        }

        memset(&header, 0, sizeof(header));

        header.magic = LOGGER_MAGIC;
        header.version = LOGGER_VERSION;
        header.times_occured = 1;
        header.error_code = error_cause;
        header.write_index = index;
        header.wrapped = wrapped;
        // TODO Add current timestamp
        // TODO Add total runtime timer
    }

    // =========================
    // WRITE HEADER
    // =========================
    fwrite(&header,
           sizeof(header),
           1,
           f);

    // =========================
    // WRITE LOG ENTRIES
    // =========================
    fwrite(input_buffer,
           sizeof(input_buffer),
           CONFIG_LOG_MAX_RING_BUFFER_SIZE,
           f);

    fclose(f);

    ESP_LOGI("LOGGER",
             "Crash log stored: %lu (count=%lu)",
             error_cause,
             header.times_occured);

    return ESP_OK;
}

static esp_err_t recover_from_panic(void)
{
    if (g_panic_snapshot.magic != PANIC_MAGIC)
    {
        return ESP_OK; // no crash
    }

    ESP_LOGW(TAG, "PANIC RECOVERY: error=%lu",
             g_panic_snapshot.error_code);

    // Convert RTC snapshot → RAM storage format
    logger_header_t header = {0};

    header.magic = LOGGER_MAGIC;
    header.version = LOGGER_VERSION;
    header.times_occured = 1;
    header.error_code = g_panic_snapshot.error_code;
    header.last_timestamp = g_panic_snapshot.timestamp;
    header.write_index = g_panic_snapshot.write_index;

    esp_err_t err = store_full_crash_log(
        g_panic_snapshot.error_code,
        g_panic_snapshot.entries,
        g_panic_snapshot.write_index,
        false);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to store crash log during panic recovery: %s", esp_err_to_name(err));
    }

    // CLEAR RTC AFTER SUCCESS
    memset(&g_panic_snapshot, 0, sizeof(g_panic_snapshot));

    return ESP_OK;
}

IRAM_ATTR void panic_capture()
{
    g_panic_snapshot.magic = PANIC_MAGIC;
    // g_panic_snapshot.error_code = error_code;
    g_panic_snapshot.reset_reason = esp_reset_reason();
    g_panic_snapshot.timestamp = esp_log_timestamp();

    const uint16_t start_index = log_index - CONFIG_LOG_PANIC_BUFFER_COUNT + 1;
    uint16_t size = CONFIG_LOG_PANIC_BUFFER_COUNT;
    if (log_index < CONFIG_LOG_PANIC_BUFFER_COUNT - 1)
    {
        size = log_index + 1;
    }

    g_panic_snapshot.write_index = size - 1;

    for (size_t i = 0; i < size; i++)
    {
        const uint16_t idx = start_index + i;
        g_panic_snapshot.entries[i] = log_buffer[idx];
    }
}

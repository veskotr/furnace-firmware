//
// Created by vesko on 22.3.2026 г..
//
#include "esp_log.h"
#include "logger_core.h"
#include "logger_internal.h"
#include "freertos/semphr.h"
#include "esp_littlefs.h"
#include "time_component.h"
#include "dirent.h"

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

    uint32_t times_occurred;
    uint32_t last_timestamp;
    uint32_t last_total_runtime_sec;
    uint32_t error_code;
} logger_header_t;

static const char* TAG = "LOGGER_STORAGE";
static const char* PARTITION_LABEL = "storage";
static const char* BASE_PATH = "/crash_dumps";

static SemaphoreHandle_t log_mutex;

static log_entry_t log_buffer[CONFIG_LOG_MAX_RING_BUFFER_SIZE];
static log_entry_t read_buffer[CONFIG_LOG_MAX_RING_BUFFER_SIZE];

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
static esp_err_t store_full_crash_log(uint32_t error_cause, const log_entry_t* input_buffer, uint16_t index,
                                      uint8_t wrapped);

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

void logger_store_full_log(const uint32_t error_cause)
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
        .base_path = BASE_PATH,
        .partition_label = PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount LittleFS (%s)", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(PARTITION_LABEL, &total, &used);

    ESP_LOGI(TAG, "LittleFS: total=%d, used=%d", total, used);
    return ret;
}

static esp_err_t store_full_crash_log(const uint32_t error_cause, const log_entry_t* input_buffer, const uint16_t index,
                                      const uint8_t wrapped)
{
    char path[64] = {0};

    snprintf(path, sizeof(path),
             "%s/err_%lu.bin", BASE_PATH, error_cause);

    FILE* f = fopen(path, "rb+");

    const bool exists = (f != NULL);

    logger_header_t header = {};

    // =========================
    // CASE A: FILE EXISTS
    // =========================
    if (exists)
    {
        // read ONLY header
        const size_t r = fread(&header,
                               sizeof(logger_header_t),
                               1,
                               f);

        if (r != 1 || header.magic != LOGGER_MAGIC)
        {
            // corrupted → reset header
            memset(&header, 0, sizeof(header));
        }

        header.times_occurred++;
        header.error_code = error_cause;
        header.last_timestamp = get_current_time_ms();
        header.last_total_runtime_sec = get_total_runtime_sec();
        header.write_index = index;
        header.wrapped = wrapped;
        // move to beginning for overwrite
        rewind(f);
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
        header.times_occurred = 1;
        header.error_code = error_cause;
        header.write_index = index;
        header.wrapped = wrapped;
        header.last_timestamp = get_current_time_ms();
        header.last_total_runtime_sec = get_total_runtime_sec();
    }

    // =========================
    // WRITE HEADER
    // =========================
    if (fwrite(&header,
               sizeof(header),
               1,
               f) != 1)
    {
        ESP_LOGE("LOGGER", "Failed to write header");
        fclose(f);
        return ESP_FAIL;
    }

    // =========================
    // WRITE LOG ENTRIES
    // =========================
    const uint16_t expected_entries = wrapped ? CONFIG_LOG_MAX_RING_BUFFER_SIZE : index + 1;
    if (fwrite(input_buffer,
               sizeof(log_entry_t),
               expected_entries,
               f) != expected_entries)
    {
        ESP_LOGE("LOGGER", "Failed to write log entries");
        fclose(f);
        return ESP_FAIL;
    }

    fclose(f);

    ESP_LOGI("LOGGER",
             "Crash log stored: %lu (count=%lu)",
             error_cause,
             header.times_occurred);

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
    header.times_occurred = 1;
    //TODO: add error code mapping if needed
    header.write_index = g_panic_snapshot.write_index;

    const esp_err_t err = store_full_crash_log(
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

    uint16_t size = CONFIG_LOG_PANIC_BUFFER_COUNT;

    if (!log_wrapped)
    {
        size = log_index < CONFIG_LOG_PANIC_BUFFER_COUNT
                   ? log_index
                   : CONFIG_LOG_PANIC_BUFFER_COUNT;
    }
    g_panic_snapshot.write_index = size - 1;

    for (uint16_t i = 0; i < size; i++)
    {
        int idx = (log_index - size + 1 + i + CONFIG_LOG_MAX_RING_BUFFER_SIZE) % CONFIG_LOG_MAX_RING_BUFFER_SIZE;

        if (idx < 0)
            idx += CONFIG_LOG_MAX_RING_BUFFER_SIZE;

        g_panic_snapshot.entries[i] = log_buffer[idx];
    }
}

esp_err_t list_crash_logs(void)
{
    DIR *dir = opendir(BASE_PATH);
    if (!dir)
    {
        ESP_LOGE(TAG, "Failed to open log directory");
        return ESP_FAIL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG)
        {
            ESP_LOGI(TAG, "Found crash log: %s", entry->d_name);
        }
    }
    closedir(dir);
    return ESP_OK;
}

esp_err_t read_log(char *filename)
{
    char path[64] = {0};
    snprintf(path, sizeof(path), "%s/%s", BASE_PATH, filename);

    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("Failed to open file\n");
        return ESP_FAIL;
    }

    // =========================
    // 1. Read header
    // =========================
    logger_header_t header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
        printf("Failed to read header\n");
        fclose(f);
        return ESP_FAIL;
    }

    if (header.magic != LOGGER_MAGIC) {
        printf("Invalid log file\n");
        fclose(f);
        return ESP_FAIL;
    }

    printf("Log info:\n");
    printf("  Error: %lu\n", header.error_code);
    printf("  Occurrences: %lu\n", header.times_occurred);
    printf("  Last timestamp: %lu\n", header.last_timestamp);
    printf("  Wrapped: %d\n", header.wrapped);

    // =========================
    // 2. Read entries
    // =========================
    const uint16_t count = header.wrapped
        ? CONFIG_LOG_MAX_RING_BUFFER_SIZE
        : header.write_index + 1;

    const size_t read = fread(read_buffer,
                        sizeof(log_entry_t),
                        count,
                        f);

    if (read != count) {
        printf("Partial read: %zu/%u\n", read, count);
    }

    fclose(f);

    // =========================
    // 3. Print correctly ordered
    // =========================
    if (header.wrapped) {
        // ring buffer → start from write_index
        for (uint16_t i = 0; i < count; i++) {
            uint16_t idx = (header.write_index + 1 + i) % count;
            log_entry_t *entry = &read_buffer[idx];

            printf("[%lu ms] %s/%s: %s\n",
                   entry->timestamp,
                   level_to_string(entry->level),
                   entry->tag,
                   entry->message);
        }
    } else {
        for (uint16_t i = 0; i < count; i++) {
            log_entry_t *entry = &read_buffer[i];

            printf("[%lu ms] %s/%s: %s\n",
                   entry->timestamp,
                   level_to_string(entry->level),
                   entry->tag,
                   entry->message);
        }
    }

    return ESP_OK;
}

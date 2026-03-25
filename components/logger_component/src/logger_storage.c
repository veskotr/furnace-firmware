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

#define PANIC_MAGIC 0xBADC0FFE

typedef struct
{
    uint32_t magic;
    uint32_t error_code;
    uint32_t reset_reason;
    uint32_t timestamp;

    uint16_t entry_count;
    uint16_t write_index;
    uint8_t wrapped;

    log_entry_t entries[CONFIG_LOG_MAX_RING_BUFFER_SIZE];

} panic_snapshot_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t entry_count;
    uint8_t wrapped;
    uint16_t write_index;

    uint32_t times_occured;
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


static const char* TAG = "LOGGER_STORAGE";

SemaphoreHandle_t log_mutex;

static log_entry_t log_buffer[CONFIG_LOG_MAX_RING_BUFFER_SIZE];
static logger_header_t header_buffer = {0};

static size_t log_index = 0;
static bool log_wrapped = false;

RTC_DATA_ATTR static panic_snapshot_t g_panic_snapshot;

// ================================================
// Static Buffers
// ================================================
static char file_path_buffer[64] = {0};
static log_entry_t temp_buffer[CONFIG_LOG_MAX_RING_BUFFER_SIZE];

// ================================================
// Helper deffinitions
// ================================================
static const char* level_to_string(const uint8_t level);

static void init_littlefs(void);

static bool file_exists(const char *path);

static esp_err_t recover_from_panic(void);

esp_err_t logger_init_storage(void){
    log_mutex = xSemaphoreCreateMutex();
    if (log_mutex == NULL)
    {
        ESP_LOGE(TAG, "%s", "Failed to create logger mutex");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = init_littlefs();

    esp_register_shutdown_handler(panic_capture);

    return 
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

static esp_err_t store_full_crash_log(uint32_t error_cause)
{
    snprintf(path_buffer, sizeof(path_buffer),
             "/littlefs/err_%lu.bin", error_cause);

    FILE *f = fopen(path_buffer, "rb+");

    bool exists = (f != NULL);

    // =========================
    // CASE A: FILE EXISTS
    // =========================
    if (exists)
    {
        // read ONLY header
        size_t r = fread(&header_buffer,
                         sizeof(logger_header_t),
                         1,
                         f);

        if (r != 1 || header_buffer.magic != LOGGER_MAGIC)
        {
            // corrupted → reset header
            memset(&header_buffer, 0, sizeof(header_buffer));
        }

        header_buffer.times_occured++;
        header_buffer.error_code = error_cause;

        // move to beginning for overwrite
        rewind(f);
    }
    else
    {
        // =========================
        // CASE B: NEW FILE
        // =========================
        f = fopen(path_buffer, "wb");
        if (!f)
        {
            ESP_LOGE("LOGGER", "Failed to create file");
            return ESP_FAIL;
        }

        memset(&header_buffer, 0, sizeof(header_buffer));

        header_buffer.magic = LOGGER_MAGIC;
        header_buffer.version = LOGGER_VERSION;
        header_buffer.times_occured = 1;
        header_buffer.error_code = error_cause;
    }

    // =========================
    // UPDATE HEADER COMMON FIELDS
    // =========================
    header_buffer.entry_count = log_index;
    header_buffer.wrapped = log_wrapped;
    header_buffer.write_index = log_index;
    //TODO Add current timestamp
    //TODO Add total runtime timer

    // =========================
    // WRITE HEADER
    // =========================
    fwrite(&header_buffer,
           sizeof(logger_header_t),
           1,
           f);

    // =========================
    // COPY RAM BUFFER SAFELY
    // =========================
    memcpy(temp_buffer,
           log_buffer,
           sizeof(log_buffer));

    // =========================
    // WRITE LOG ENTRIES
    // =========================
    fwrite(temp_buffer,
           sizeof(log_entry_t),
           CONFIG_LOG_MAX_RING_BUFFER_SIZE,
           f);

    fclose(f);

    ESP_LOGI("LOGGER",
             "Crash log stored: %lu (count=%lu)",
             error_cause,
             header_buffer.times_occured);

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
    logger_header_t hdr = {0};

    hdr.magic = LOGGER_MAGIC;
    hdr.version = LOGGER_VERSION;
    hdr.times_occured = 1;
    hdr.error_code = g_panic_snapshot.error_code;
    hdr.last_timestamp = g_panic_snapshot.timestamp;
    hdr.entry_count = g_panic_snapshot.entry_count;
    hdr.write_index = g_panic_snapshot.write_index;
    hdr.wrapped = g_panic_snapshot.wrapped;

    snprintf(file_path_buffer, sizeof(file_path_buffer),
             "/littlefs/err_%lu.bin",
             g_panic_snapshot.error_code);

    FILE *f = fopen(file_path_buffer, "rb+");
    bool exists = (f != NULL);

    if (exists)
    {
        fread(&hdr, sizeof(logger_header_t), 1, f);

        if (hdr.magic != LOGGER_MAGIC)
        {
            memset(&hdr, 0, sizeof(hdr));
            hdr.magic = LOGGER_MAGIC;
        }

        hdr.times_occured++;
        hdr.last_timestamp = g_panic_snapshot.timestamp;

        rewind(f);
    }
    else
    {
        f = fopen(file_path_buffer, "wb");
        if (!f)
        {
            ESP_LOGE(TAG, "Failed to create crash file");
            return ESP_FAIL;
        }

        hdr.times_occured = 1;
    }

    // WRITE HEADER
    fwrite(&hdr, sizeof(hdr), 1, f);

    // WRITE LOG BUFFER FROM RTC
    fwrite(g_panic_snapshot.entries,
           sizeof(log_entry_t),
           CONFIG_LOG_MAX_RING_BUFFER_SIZE,
           f);

    fclose(f);

    // CLEAR RTC AFTER SUCCESS
    memset(&g_panic_snapshot, 0, sizeof(g_panic_snapshot));

    return ESP_OK;
}

IRAM_ATTR void panic_capture(uint32_t error_code)
{
    g_panic_snapshot.magic = PANIC_MAGIC;
    g_panic_snapshot.error_code = error_code;
    g_panic_snapshot.reset_reason = esp_reset_reason();
    g_panic_snapshot.timestamp = esp_log_timestamp();

    g_panic_snapshot.entry_count = log_index;
    g_panic_snapshot.write_index = log_index;
    g_panic_snapshot.wrapped = log_wrapped;

    // copy ONLY safe data
    for (int i = 0; i < CONFIG_LOG_MAX_RING_BUFFER_SIZE; i++)
    {
        g_panic_snapshot.entries[i] = log_buffer[i];
    }
}

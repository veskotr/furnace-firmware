#include "time-component.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "time.h"

static const char* TAG = "time_component";
static const uint32_t TIMER_MAGIC = 0xDEADBEEF;
static const uint32_t TIMER_SAVE_PERIOD_MS = 0x36EE80; // 1 hour in ms
static const uint32_t TIMER_RUN_INTERVAL_MS = 1000; // 1 second in ms
static const char* NVS_NAMESPACE = "time_component";
static const char* NVS_KEY = "time_data";

static TaskHandle_t time_component_task_handle;
static bool time_component_is_running = false;
static uint32_t time_since_last_save_ms = 0;
static uint32_t total_runtime_sec = 0;

typedef struct
{
    uint32_t magic;
    uint32_t total_runtime_sec;
} time_storage_t;

static esp_err_t load_total_runtime_from_nvs(void);

static void time_component_task(void* args)
{
    while (time_component_is_running)
    {
        vTaskDelay(pdMS_TO_TICKS(TIMER_RUN_INTERVAL_MS));
        time_since_last_save_ms = get_time_since_boot_ms();
        total_runtime_sec += TIMER_RUN_INTERVAL_MS / 1000;
        if (time_since_last_save_ms >= TIMER_SAVE_PERIOD_MS)
        {
            store_total_runtime_to_nvs();
            time_since_last_save_ms = 0;
        }
    }
    vTaskDelete(NULL);
}

esp_err_t time_component_init(void)
{
    if (time_component_is_running)
    {
        return ESP_OK;
    }

    const esp_err_t err = load_total_runtime_from_nvs();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load total runtime from NVS: %s", esp_err_to_name(err));
        return err;
    }

    time_component_is_running = true;

    const BaseType_t result = xTaskCreate(
        time_component_task,
        CONFIG_TIME_COMPONENT_TASK_NAME,
        CONFIG_TIME_COMPONENT_TASK_STACK_SIZE,
        NULL,
        CONFIG_TIME_COMPONENT_TASK_PRIORITY,
        &time_component_task_handle
    );

    if (result != pdPASS)
    {
        time_component_is_running = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

void store_total_runtime_to_nvs(void)
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS\n");
        return;
    }

    const time_storage_t storage_data = {
        .magic = TIMER_MAGIC,
        .total_runtime_sec = total_runtime_sec
    };

    err = nvs_set_blob(handle, NVS_KEY, &storage_data, sizeof(time_storage_t));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error writing value\n");
    }

    err = nvs_commit(handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error committing\n");
    }

    nvs_close(handle);
}

uint32_t get_current_time_ms(void)
{
    time_t now;
    time(&now);
    return (uint32_t)(now * 1000); // Convert seconds to milliseconds
}

uint32_t get_time_since_boot_ms(void)
{
    const int64_t time_ms = esp_timer_get_time() / 1000; // Convert microseconds to milliseconds
    return (uint32_t)(time_ms & 0xFFFFFFFF); // Return lower
}

uint32_t get_total_runtime_sec(void)
{
    return total_runtime_sec;
}

esp_err_t time_component_shutdown(void)
{
    if (!time_component_is_running)
    {
        return ESP_OK;
    }

    time_component_is_running = false;

    if (time_component_task_handle != NULL)
    {
        vTaskDelete(time_component_task_handle);
        time_component_task_handle = NULL;
    }

    return ESP_OK;
}

static esp_err_t load_total_runtime_from_nvs(void)
{
    nvs_handle_t handle;

    // Open NVS
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS\n");
        return err;
    }

    time_storage_t data;
    size_t size = sizeof(data);

    err = nvs_get_blob(handle, NVS_KEY, &data, &size);

    if (err != ESP_OK || data.magic != TIMER_MAGIC)
    {
        // Invalid → initialize defaults
        data.magic = TIMER_MAGIC;
        data.total_runtime_sec = 0;

        nvs_set_blob(handle, "config", &data, sizeof(data));
        nvs_commit(handle);
    }
    return ESP_OK;
}

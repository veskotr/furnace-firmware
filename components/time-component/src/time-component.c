#include "time-component.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_log.h"

static const char* TAG = "time_component";
static const uint32_t TIMER_SAVE_PERIOD_MS = 0x36EE80; // 1 hour in ms
static const uint32_t TIMER_RUN_INTERVAL_MS = 1000; // 1 second in ms

static TaskHandle_t time_component_task_handle;
static bool time_component_is_running = false;
static uint32_t time_since_last_save_ms = 0;
static uint32_t total_runtime_sec = 0;

typedef struct
{
    uint32_t magic;
    uint32_t total_runtime_sec;
} time_storage_t;

static void time_component_task(void* args)
{
    while (time_component_is_running)
    {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Update every second
    }
    vTaskDelete(NULL);
}

esp_err_t time_component_init(void)
{
    if (time_component_is_running)
    {
        return ESP_OK;
    }

    time_component_is_running = true;

    BaseType_t result = xTaskCreate(
        time_component_task,
        "time_component_task",
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

    // Open NVS
    esp_err_t err = nvs_open("timer_storage", NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS\n");
        return;
    }

    time_storage_t starage_data = {
        .magic = TIMER_MAGIC,
        .total_runtime_sec = total_runtime_sec
    };

    err= nvs_set_blob(handle, "time_data", &starage_data, sizeof(time_storage_t));
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
}

uint32_t get_time_since_boot_ms(void)
{
}

uint32_t get_total_runtime(void)
{
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

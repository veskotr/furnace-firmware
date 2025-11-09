#include "temperature-monitor-component.h"
#include "logger-component.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "spi-master-component.h"

// Component tag
static const char *TAG = "TEMP_MONITOR";

// Task handle
static TaskHandle_t temp_monitor_task_handle = NULL;

// Event loop handle
static esp_event_loop_handle_t temp_monitor_event_loop = NULL;

// Monitor running flag
static bool monitor_running = false;

// ----------------------------
// Configuration
// ----------------------------
typedef struct {
    const char *task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} TempMonitorConfig_t;

static const TempMonitorConfig_t temp_monitor_config = {
    .task_name = "TEMP_MONITOR_TASK",
    .stack_size = 4096,
    .task_priority = 5
};

// ----------------------------
// Task
// ----------------------------
static void temp_monitor_task(void *args)
{
    ESP_LOGI(TAG, "Temperature monitor task started");

    while (monitor_running) {
        // TODO: read sensor and publish events
        vTaskDelay(pdMS_TO_TICKS(1000)); // example 1s delay
    }

    ESP_LOGI(TAG, "Temperature monitor task exiting");
    vTaskDelete(NULL);
}

// ----------------------------
// Public API
// ----------------------------
esp_err_t init_temp_monitor(esp_event_loop_handle_t loop_handle)
{
    if (monitor_running) {
        return ESP_OK;
    }
    logger_init();
    esp_err_t ret = init_spi(1);
    if (ret != ESP_OK) {
        logger_send_error(TAG, "Failed to initialize SPI: %s", esp_err_to_name(ret));
        return ret;
    }

    temp_monitor_event_loop = loop_handle;
    monitor_running = true;

    ret = xTaskCreate(
               temp_monitor_task,
               temp_monitor_config.task_name,
               temp_monitor_config.stack_size,
               NULL,
               temp_monitor_config.task_priority,
               &temp_monitor_task_handle
           ) == pdPASS ? ESP_OK : ESP_FAIL;

    if (ret != ESP_OK) {
        logger_send_error(TAG, "Failed to create temperature monitor task");
        monitor_running = false;
        return ret;
    }
    return ESP_OK;
}

esp_err_t shutdown_temp_monitor(void)
{
    if (!monitor_running) {
        return ESP_OK;
    }

    monitor_running = false;

    if (temp_monitor_task_handle) {
        vTaskDelete(temp_monitor_task_handle);
        temp_monitor_task_handle = NULL;
    }

    return ESP_OK;
}

float read_temp_sensor_data(void)
{
    // TODO: implement sensor reading
    return 0.0f;
}

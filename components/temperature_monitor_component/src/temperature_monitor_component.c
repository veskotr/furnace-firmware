#include "temperature_monitor_component.h"
#include "logger_component.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "spi_master_component.h"
#include "core_types.h"
#include "temperature_sensors.h"

// Task handle
TaskHandle_t temp_monitor_task_handle;

// Monitor running flag
static bool monitor_running = false;

// ----------------------------
// Configuration
// ----------------------------
typedef struct
{
    const char *task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} TempMonitorConfig_t;

static const TempMonitorConfig_t temp_monitor_config = {
    .task_name = "TEMP_MONITOR_TASK",
    .stack_size = 4096,
    .task_priority = 5};

temp_monitor_t temp_monitor = {0};

// ----------------------------
// Task
// ----------------------------
static void temp_monitor_task(void *args)
{
    logger_send_info(TAG, "Temperature monitor task started");

    while (monitor_running)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    logger_send_info(TAG, "Temperature monitor task exiting");
    vTaskDelete(NULL);
}

// ----------------------------
// Public API
// ----------------------------
esp_err_t init_temp_monitor(temp_monitor_config_t *config)
{
    if (monitor_running)
    {
        return ESP_OK;
    }

    temp_monitor.number_of_attatched_sensors = config->number_of_attatched_sensors;
    temp_monitor.temperature_event_loop_handle = config->temperature_events_loop_handle;
    temp_monitor.coordinator_event_loop_handle = config->coordinator_events_loop_handle;

    logger_init();

    esp_err_t ret = init_spi(config->number_of_attatched_sensors);
    if (ret != ESP_OK)
    {
        logger_send_error(TAG, "Failed to initialize SPI: %s", esp_err_to_name(ret));
        return ret;
    }

    monitor_running = true;

    ret = xTaskCreate(
              temp_monitor_task,
              temp_monitor_config.task_name,
              temp_monitor_config.stack_size,
              NULL,
              temp_monitor_config.task_priority,
              &temp_monitor_task_handle) == pdPASS
              ? ESP_OK
              : ESP_FAIL;

    if (ret != ESP_OK)
    {
        logger_send_error(TAG, "Failed to create temperature monitor task");
        monitor_running = false;
        return ret;
    }
    return ESP_OK;
}

esp_err_t shutdown_temp_monitor(void)
{
    if (!monitor_running)
    {
        return ESP_OK;
    }

    monitor_running = false;

    if (temp_monitor_task_handle)
    {
        vTaskDelete(temp_monitor_task_handle);
        temp_monitor_task_handle = NULL;
    }

    return ESP_OK;
}

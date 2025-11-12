#include "temperature_monitor_component.h"
#include "logger_component.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "spi_master_component.h"
#include "core_types.h"
#include "temperature_sensors.h"
#include "temperature_monitor_internal_types.h"
#include "temperature_monitor_task.h"

// Monitor running flag
static bool monitor_running = false;

ESP_EVENT_DEFINE_BASE(MEASURED_TEMPERATURE_EVENT);
ESP_EVENT_DEFINE_BASE(TEMP_MEASURE_ERROR_EVENT);

static const TempMonitorConfig_t temp_monitor_config = {
    .task_name = "TEMP_MONITOR_TASK",
    .stack_size = 4096,
    .task_priority = 5};

static void coordinator_event_handler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data)
{
    if (id == COORDINATOR_EVENT_MEASURE_TEMPERATURE)
    {

        if (monitor_running && temp_monitor_task_handle)
        {
            xTaskNotifyGive(temp_monitor_task_handle);
        }
    }
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

    temp_monitor.number_of_attached_sensors = config->number_of_attached_sensors;
    temp_monitor.temperature_event_loop_handle = config->temperature_events_loop_handle;
    temp_monitor.coordinator_event_loop_handle = config->coordinator_events_loop_handle;
    temp_sensors_array.number_of_attached_sensors = config->number_of_attached_sensors;

    logger_init();

    esp_err_t ret = init_spi(config->number_of_attached_sensors);
    if (ret != ESP_OK)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to initialize SPI: %s", esp_err_to_name(ret));
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
        LOGGER_LOG_ERROR(TAG, "Failed to create temperature monitor task");
        shutdown_spi();
        monitor_running = false;
        return ret;
    }

    ret = esp_event_handler_instance_register_with(
        temp_monitor.coordinator_event_loop_handle,
        COORDINATOR_EVENTS,
        ESP_EVENT_ANY_ID,
        coordinator_event_handler,
        NULL,
        NULL);
    return ESP_OK;
}

esp_err_t shutdown_temp_monitor_controller(void)
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

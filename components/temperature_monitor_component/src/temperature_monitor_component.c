#include "temperature_monitor_component.h"
#include "logger_component.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "spi_master_component.h"
#include "core_types.h"
#include "temperature_monitor_internal_types.h"
#include "temperature_monitor_task.h"
#include "temperature_monitor_types.h"
#include "utils.h"
#include "temperature_monitor_log.h"

static const char *TAG = TEMP_MONITOR_LOG;

EventGroupHandle_t temp_processor_event_group = NULL;

temp_monitor_t temp_monitor = {0};

// Monitor running flag
volatile bool monitor_running = false;

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

    logger_init();

    temp_processor_event_group = xEventGroupCreate();
    if (temp_processor_event_group == NULL)
    {
        LOGGER_LOG_ERROR("TEMP_MONITOR", "Failed to create temperature processor event group");
        return ESP_FAIL;
    }

    CHECK_ERR_LOG_RET(init_spi(config->number_of_attached_sensors), "Failed to initialize SPI");
    

    CHECK_ERR_LOG_CALL_RET(start_temperature_monitor_task(), 
                           shutdown_spi(),
                           "Failed to start temperature monitor task");

    return ESP_OK;
}

esp_err_t shutdown_temp_monitor_controller(void)
{
    if (!monitor_running)
    {
        return ESP_OK;
    }

    monitor_running = false;

    CHECK_ERR_LOG_RET(stop_temperature_monitor_task(), "Failed to stop temperature monitor task");

    CHECK_ERR_LOG_RET(shutdown_spi(), "Failed to shutdown SPI");

    vEventGroupDelete(temp_processor_event_group);
    temp_processor_event_group = NULL;

    return ESP_OK;
}
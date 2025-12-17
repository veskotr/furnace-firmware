#include "temperature_monitor_component.h"
#include "logger_component.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "spi_master_component.h"
#include "core_types.h"
#include "temperature_monitor_internal.h"
#include "temperature_monitor_types.h"
#include "utils.h"

static const char* TAG = "TEMP_MONITOR";

// Single global context pointer (internal to component)
temp_monitor_context_t* g_temp_monitor_ctx = NULL;

// ----------------------------
// Public API
// ----------------------------
esp_err_t init_temp_monitor(temp_monitor_config_t* config)
{
    if (g_temp_monitor_ctx != NULL && g_temp_monitor_ctx->monitor_running)
    {
        return ESP_OK;
    }

    // Allocate context if needed
    if (g_temp_monitor_ctx == NULL)
    {
        g_temp_monitor_ctx = calloc(1, sizeof(temp_monitor_context_t));
        if (g_temp_monitor_ctx == NULL)
        {
            LOGGER_LOG_ERROR(TAG, "Failed to allocate temperature monitor context");
            return ESP_ERR_NO_MEM;
        }
    }

    // Initialize configuration
    g_temp_monitor_ctx->number_of_attached_sensors = config->number_of_attached_sensors;
    CHECK_ERR_LOG_RET(init_spi(g_temp_monitor_ctx->number_of_attached_sensors),
                      "Failed to initialize SPI for temperature sensors");

    // Create event group
    g_temp_monitor_ctx->processor_event_group = xEventGroupCreate();
    if (g_temp_monitor_ctx->processor_event_group == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to create temperature processor event group");
        free(g_temp_monitor_ctx);
        shutdown_spi();
        g_temp_monitor_ctx = NULL;
        return ESP_FAIL;
    }

    CHECK_ERR_LOG_CALL_RET(init_temp_sensors(g_temp_monitor_ctx),
                           free(g_temp_monitor_ctx);
                           shutdown_spi(),
                           "Failed to initialize temperature sensors");

    CHECK_ERR_LOG_CALL_RET(start_temperature_monitor_task(g_temp_monitor_ctx),
                           free(g_temp_monitor_ctx);
                           shutdown_spi(),
                           "Failed to start temperature monitor task");

    return ESP_OK;
}

EventGroupHandle_t temp_monitor_get_event_group(void)
{
    return (g_temp_monitor_ctx != NULL) ? g_temp_monitor_ctx->processor_event_group : NULL;
}

esp_err_t shutdown_temp_monitor(void)
{
    if (g_temp_monitor_ctx == NULL || !g_temp_monitor_ctx->monitor_running)
    {
        return ESP_OK;
    }

    CHECK_ERR_LOG_RET(stop_temperature_monitor_task(g_temp_monitor_ctx), "Failed to stop temperature monitor task");

    if (g_temp_monitor_ctx->processor_event_group)
    {
        vEventGroupDelete(g_temp_monitor_ctx->processor_event_group);
        g_temp_monitor_ctx->processor_event_group = NULL;
    }

    // Free context
    free(g_temp_monitor_ctx);
    g_temp_monitor_ctx = NULL;

    shutdown_spi();

    return ESP_OK;
}

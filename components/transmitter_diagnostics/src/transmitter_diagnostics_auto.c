/**
 * @file transmitter_diagnostics_auto.c
 * @brief Auto-dump task initialization — runs the register dump once at boot.
 */

#include "transmitter_diagnostics.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "sdkconfig.h"

#include "logger_component.h"
#include "utils.h"

static const char *TAG = "TX_DIAG_AUTO";

static void transmitter_dump_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(100)); /* Let other components initialize */

    LOGGER_LOG_INFO(TAG, "Starting transmitter register dump...");
    const esp_err_t err = transmitter_diagnostics_find_and_dump();

    if (err == ESP_OK)
    {
        LOGGER_LOG_INFO(TAG, "Transmitter dump complete");
    }
    else
    {
        LOGGER_LOG_WARN(TAG, "Transmitter dump failed: 0x%X", err);
    }

    vTaskDelete(NULL);
}

esp_err_t transmitter_diagnostics_auto_init(void)
{
    LOGGER_LOG_INFO(TAG, "Spawning transmitter dump task");

    const BaseType_t ok = xTaskCreate(transmitter_dump_task, "tx_dump",
                                      4096, NULL, 2, NULL);
    if (ok != pdPASS)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to create transmitter dump task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

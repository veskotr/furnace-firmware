#pragma once

#include "esp_err.h"

/**
 * @brief Initialize the MODBUS RS485 temperature monitor (MS9024).
 *
 * Sets up UART in RS485 half-duplex mode, verifies MS9024 configuration,
 * and starts a periodic read task that posts temperature data to the
 * event system (same TEMP_PROCESSOR_EVENT / PROCESS_TEMPERATURE_EVENT_DATA
 * as the SPI-based temperature processor).
 *
 * @return ESP_OK on success
 */
esp_err_t modbus_temp_monitor_init(void);

/**
 * @brief Shut down the MODBUS temperature monitor.
 * @return ESP_OK on success
 */
esp_err_t modbus_temp_monitor_shutdown(void);

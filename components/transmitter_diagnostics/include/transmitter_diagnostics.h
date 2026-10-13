/**
 * @file transmitter_diagnostics.h
 * @brief Locate the connected thermocouple/RTD transmitter on the Modbus bus
 *        and dump its full register set for debugging.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Probe Modbus slave addresses and return the first one that responds.
 *
 * Scans addresses [CONFIG_TRANSMITTER_DIAGNOSTICS_SCAN_START_ADDR ..
 * CONFIG_TRANSMITTER_DIAGNOSTICS_SCAN_END_ADDR] by reading the MS9024 firmware
 * version register. The first address that answers is treated as the
 * transmitter.
 *
 * @param[out] out_slave_addr  Address of the first responding slave.
 * @return ESP_OK if a transmitter was found, ESP_ERR_NOT_FOUND otherwise.
 */
esp_err_t transmitter_diagnostics_find(uint8_t *out_slave_addr);

/**
 * @brief Dump the full register set of the transmitter at @p slave_addr.
 *
 * Reads the same ranges as the MS9024 diagnostic scan (config, device info /
 * calibration, extra config, float RAM) plus the key float-decoded registers
 * (IN_OFFSET, AOUT, PV, T2, ...) and logs everything at INFO level.
 */
void transmitter_diagnostics_dump(uint8_t slave_addr);

/**
 * @brief Convenience: find the transmitter, then dump it.
 *
 * @return ESP_OK if the transmitter was found and dumped, ESP_ERR_NOT_FOUND
 *         if no slave responded in the configured scan range.
 */
esp_err_t transmitter_diagnostics_find_and_dump(void);

/**
 * @brief Create and start a one-shot FreeRTOS task that dumps the transmitter
 *        at boot time.
 *
 * Call this from main() or early init code. The task will scan for the
 * transmitter, dump its registers, and then delete itself. All output
 * goes to the logger.
 *
 * Stack: 4096 bytes, priority 2, runs once then exits.
 *
 * @return ESP_OK if the task was created successfully.
 */
esp_err_t transmitter_diagnostics_auto_init(void);

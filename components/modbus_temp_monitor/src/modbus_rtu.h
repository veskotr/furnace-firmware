/**
 * @file modbus_rtu.h
 * @brief Low-level MODBUS RTU protocol helpers over ESP-IDF UART/RS485.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/uart.h"

/**
 * @brief Calculate MODBUS CRC-16 (polynomial 0xA001, initial 0xFFFF).
 */
uint16_t modbus_crc16(const uint8_t *data, size_t len);

/**
 * @brief Initialise a UART port in RS485 half-duplex mode.
 *
 * @param port      UART port number
 * @param tx_pin    TX GPIO
 * @param rx_pin    RX GPIO
 * @param de_pin    RS485 direction-enable GPIO
 * @param baud_rate Baud rate
 * @return ESP_OK on success
 */
esp_err_t modbus_rtu_init_uart(uart_port_t port, int tx_pin, int rx_pin,
                               int de_pin, int baud_rate);

/**
 * @brief Read one or more 16-bit holding registers (MODBUS function 0x03).
 *
 * @param port         UART port
 * @param slave_addr   MODBUS slave address (1-247)
 * @param start_reg    Starting register address
 * @param num_regs     Number of 16-bit registers to read (1-125)
 * @param data_out     Buffer for register data (num_regs * 2 bytes)
 * @param data_out_sz  Size of data_out buffer
 * @param timeout_ms   Response timeout in milliseconds
 * @return ESP_OK on success
 */
esp_err_t modbus_rtu_read_holding_registers(uart_port_t port,
                                            uint8_t slave_addr,
                                            uint16_t start_reg,
                                            uint16_t num_regs,
                                            uint8_t *data_out,
                                            size_t data_out_sz,
                                            int timeout_ms);

/**
 * @brief Write a single 16-bit holding register (MODBUS function 0x06).
 *
 * @param port        UART port
 * @param slave_addr  MODBUS slave address
 * @param reg         Register address
 * @param value       16-bit value to write
 * @param timeout_ms  Response timeout in milliseconds
 * @return ESP_OK on success
 */
esp_err_t modbus_rtu_write_single_register(uart_port_t port,
                                           uint8_t slave_addr,
                                           uint16_t reg,
                                           uint16_t value,
                                           int timeout_ms);

/**
 * @file modbus_rtu.c
 * @brief Low-level MODBUS RTU protocol: CRC, UART init, read/write registers.
 */

#include "modbus_rtu.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger_component.h"

static const char *TAG = "MODBUS_RTU";

/* =========================================================================
 *  CRC-16 (polynomial 0xA001, initial 0xFFFF)
 * ========================================================================= */
uint16_t modbus_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

/* =========================================================================
 *  UART / RS485 initialisation
 * ========================================================================= */
esp_err_t modbus_rtu_init_uart(uart_port_t port, int tx_pin, int rx_pin,
                               int de_pin, int baud_rate)
{
    LOGGER_LOG_INFO(TAG, "Init RS485 UART%d — TX=%d  RX=%d  DE=%d  baud=%d",
                    (int)port, tx_pin, rx_pin, de_pin, baud_rate);

    const uart_config_t uart_cfg = {
        .baud_rate           = baud_rate,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk          = UART_SCLK_DEFAULT,
    };

    esp_err_t err;

    err = uart_param_config(port, &uart_cfg);
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(port, tx_pin, rx_pin, de_pin, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_driver_install(port, 256, 256, 0, NULL, 0);
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_mode(port, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "uart_set_mode RS485_HALF_DUPLEX failed: %s", esp_err_to_name(err));
        return err;
    }

    LOGGER_LOG_INFO(TAG, "RS485 UART%d ready", (int)port);
    return ESP_OK;
}

/* =========================================================================
 *  Shared helpers
 * ========================================================================= */

/** Log a byte buffer as hex. */
static void log_hex(const char *prefix, const uint8_t *buf, int len)
{
    char hex[128];
    int pos = 0;
    for (int i = 0; i < len && pos < (int)sizeof(hex) - 4; i++)
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", buf[i]);
    LOGGER_LOG_INFO(TAG, "%s [%d bytes] %s", prefix, len, hex);
}

/** Build a 6-byte MODBUS frame header and append CRC → 8 bytes total. */
static void build_frame(uint8_t *req, uint8_t slave, uint8_t func,
                        uint16_t reg, uint16_t val)
{
    req[0] = slave;
    req[1] = func;
    req[2] = (reg >> 8) & 0xFF;
    req[3] = reg & 0xFF;
    req[4] = (val >> 8) & 0xFF;
    req[5] = val & 0xFF;
    uint16_t crc = modbus_crc16(req, 6);
    req[6] = crc & 0xFF;
    req[7] = (crc >> 8) & 0xFF;
}

/** Transmit an 8-byte request frame and wait for TX completion. */
static esp_err_t transmit_frame(uart_port_t port, const uint8_t *req)
{
    uart_flush_input(port);

    int sent = uart_write_bytes(port, req, 8);
    if (sent != 8) {
        LOGGER_LOG_ERROR(TAG, "uart_write_bytes: sent %d / 8", sent);
        return ESP_FAIL;
    }

    esp_err_t err = uart_wait_tx_done(port, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "uart_wait_tx_done: %s", esp_err_to_name(err));
    }
    return err;
}

/* =========================================================================
 *  Read Holding Registers (function code 0x03)
 * ========================================================================= */
esp_err_t modbus_rtu_read_holding_registers(uart_port_t port,
                                            uint8_t slave_addr,
                                            uint16_t start_reg,
                                            uint16_t num_regs,
                                            uint8_t *data_out,
                                            size_t data_out_sz,
                                            int timeout_ms)
{
    uint8_t req[8];
    build_frame(req, slave_addr, 0x03, start_reg, num_regs);
    log_hex("TX →", req, 8);

    esp_err_t err = transmit_frame(port, req);
    if (err != ESP_OK) return err;

    /* Expected: addr(1) + func(1) + byte_count(1) + data(N*2) + crc(2) */
    size_t exp_len = 5 + (size_t)(num_regs * 2);
    uint8_t resp[64];
    if (exp_len > sizeof(resp)) {
        LOGGER_LOG_ERROR(TAG, "Expected response %d exceeds buffer", (int)exp_len);
        return ESP_ERR_NO_MEM;
    }

    int rxd = uart_read_bytes(port, resp, exp_len, pdMS_TO_TICKS(timeout_ms));
    if (rxd <= 0) {
        LOGGER_LOG_ERROR(TAG, "No response from slave %d (waited %d ms)",
                         slave_addr, timeout_ms);
        return ESP_ERR_TIMEOUT;
    }

    log_hex("RX ←", resp, rxd);

    if (rxd < (int)exp_len) {
        LOGGER_LOG_WARN(TAG, "Short response: got %d bytes, expected %d", rxd, (int)exp_len);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Exception response */
    if (resp[1] & 0x80) {
        LOGGER_LOG_ERROR(TAG, "MODBUS EXCEPTION from slave %d  func=0x%02X  code=0x%02X",
                         slave_addr, resp[1], resp[2]);
        return ESP_FAIL;
    }

    /* Validate header */
    if (resp[0] != slave_addr || resp[1] != 0x03 || resp[2] != num_regs * 2) {
        LOGGER_LOG_ERROR(TAG, "Response header mismatch: addr=%d func=0x%02X count=%d",
                         resp[0], resp[1], resp[2]);
        return ESP_FAIL;
    }

    /* Verify CRC */
    uint16_t rx_crc = ((uint16_t)resp[rxd - 1] << 8) | resp[rxd - 2];
    uint16_t calc   = modbus_crc16(resp, rxd - 2);
    if (rx_crc != calc) {
        LOGGER_LOG_ERROR(TAG, "CRC MISMATCH  received=0x%04X  calculated=0x%04X",
                         rx_crc, calc);
        return ESP_FAIL;
    }

    /* Copy data payload */
    size_t data_len = (size_t)(num_regs * 2);
    if (data_len > data_out_sz) return ESP_ERR_NO_MEM;
    memcpy(data_out, &resp[3], data_len);

    return ESP_OK;
}

/* =========================================================================
 *  Write Single Register (function code 0x06)
 * ========================================================================= */
esp_err_t modbus_rtu_write_single_register(uart_port_t port,
                                           uint8_t slave_addr,
                                           uint16_t reg,
                                           uint16_t value,
                                           int timeout_ms)
{
    uint8_t req[8];
    build_frame(req, slave_addr, 0x06, reg, value);
    log_hex("WRITE →", req, 8);

    esp_err_t err = transmit_frame(port, req);
    if (err != ESP_OK) return err;

    /* MS9024 echoes back the same 8-byte frame on success */
    uint8_t resp[8];
    int rxd = uart_read_bytes(port, resp, sizeof(resp), pdMS_TO_TICKS(timeout_ms));
    if (rxd <= 0) {
        LOGGER_LOG_ERROR(TAG, "No response to write command (waited %d ms)", timeout_ms);
        return ESP_ERR_TIMEOUT;
    }

    log_hex("WRITE ←", resp, rxd);

    /* Check for exception */
    if (rxd >= 2 && (resp[1] & 0x80)) {
        LOGGER_LOG_ERROR(TAG, "MODBUS EXCEPTION on write: func=0x%02X code=0x%02X",
                         resp[1], resp[2]);
        return ESP_FAIL;
    }

    /* Perfect echo = confirmed */
    if (rxd == 8 && memcmp(req, resp, 8) == 0) {
        LOGGER_LOG_INFO(TAG, "Write confirmed — reg %d = %d (0x%04X)", reg, value, value);
        return ESP_OK;
    }

    /* CRC-verified echo with matching register/value */
    if (rxd >= 8) {
        uint16_t rx_crc = ((uint16_t)resp[rxd - 1] << 8) | resp[rxd - 2];
        uint16_t calc   = modbus_crc16(resp, rxd - 2);
        if (rx_crc != calc) {
            LOGGER_LOG_ERROR(TAG, "Write response CRC mismatch");
            return ESP_FAIL;
        }
        if (resp[0] == slave_addr && resp[1] == 0x06) {
            uint16_t echo_reg = ((uint16_t)resp[2] << 8) | resp[3];
            uint16_t echo_val = ((uint16_t)resp[4] << 8) | resp[5];
            if (echo_reg == reg && echo_val == value) {
                LOGGER_LOG_INFO(TAG, "Write confirmed — reg %d = %d", reg, value);
                return ESP_OK;
            }
        }
    }

    LOGGER_LOG_WARN(TAG, "Write response unexpected (got %d bytes)", rxd);
    return ESP_FAIL;
}

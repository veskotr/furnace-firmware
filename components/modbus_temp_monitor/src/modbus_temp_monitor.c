/**
 * @file modbus_temp_monitor.c
 * @brief MODBUS RTU temperature reading from MS9024 transmitter via RS485.
 *
 * Implements raw MODBUS RTU protocol over ESP-IDF UART driver in RS485
 * half-duplex mode. Reads the PV (process value / temperature) register
 * periodically and posts readings to the event system so the coordinator
 * and HMI receive temperature data without needing SPI thermocouple hardware.
 */

#include "sdkconfig.h"
#include "modbus_temp_monitor.h"

#ifdef CONFIG_MODBUS_TEMP_ENABLED

#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_event.h"

#include "logger_component.h"
#include "event_manager.h"
#include "event_registry.h"
#include "furnace_error_types.h"
#include "utils.h"

static const char *TAG = "MODBUS_TEMP";

/* ── Kconfig aliases ─────────────────────────────────────────────────────── */
#define UART_PORT       ((uart_port_t)CONFIG_MODBUS_TEMP_UART_PORT)
#define TX_PIN          CONFIG_MODBUS_TEMP_RS485_TX_PIN
#define RX_PIN          CONFIG_MODBUS_TEMP_RS485_RX_PIN
#define DE_PIN          CONFIG_MODBUS_TEMP_RS485_DE_PIN
#define SLAVE_ADDR      ((uint8_t)CONFIG_MODBUS_TEMP_SLAVE_ADDR)
#define BAUD_RATE       CONFIG_MODBUS_TEMP_BAUD_RATE
#define PV_REG          ((uint16_t)CONFIG_MODBUS_TEMP_PV_REGISTER)
#define READ_INTERVAL   CONFIG_MODBUS_TEMP_READ_INTERVAL_MS
#define RESP_TIMEOUT_MS CONFIG_MODBUS_TEMP_RESPONSE_TIMEOUT_MS
#define MAX_ERRORS      CONFIG_MODBUS_TEMP_MAX_CONSECUTIVE_ERRORS

/* ── MS9024 register addresses (from datasheet) ─────────────────────────── */
#define MS9024_REG_SENS       28   /* Sensor type   (LSByte, R/W)          */
#define MS9024_REG_WIRE       32   /* Wiring mode   (LSByte, R/W)          */
#define MS9024_REG_FIN        26   /* Input filter   (LSByte)              */
#define MS9024_REG_FIRMWARE   126  /* Firmware version (Int)               */
#define MS9024_REG_CF         447  /* Celsius/Fahrenheit (Coil, 0=C 1=F)  */

/* Sensor type IDs (from datasheet table) */
#define SENS_PT100_385   16
#define SENS_PT100_392   20
#define SENS_PT100_391   21

/* Wiring mode values */
#define WIRE_4WIRE       4
#define WIRE_3WIRE       3
#define WIRE_2WIRE       2

/* ── Desired sensor type and wire mode from Kconfig ──────────────────────── */
#define DESIRED_SENS     CONFIG_MODBUS_TEMP_SENSOR_TYPE
#define DESIRED_WIRE     CONFIG_MODBUS_TEMP_WIRE_MODE

/* ── State ───────────────────────────────────────────────────────────────── */
static TaskHandle_t s_task_handle = NULL;
static bool s_running = false;

/* =========================================================================
 *  MODBUS CRC-16 (polynomial 0xA001, initial 0xFFFF)
 * ========================================================================= */
static uint16_t modbus_crc16(const uint8_t *data, size_t len)
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
static esp_err_t init_rs485_uart(void)
{
    LOGGER_LOG_INFO(TAG, "Init RS485 UART%d — TX=%d  RX=%d  DE=%d  baud=%d",
                    (int)UART_PORT, TX_PIN, RX_PIN, DE_PIN, BAUD_RATE);

    const uart_config_t uart_cfg = {
        .baud_rate           = BAUD_RATE,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk          = UART_SCLK_DEFAULT,
    };

    esp_err_t err;

    err = uart_param_config(UART_PORT, &uart_cfg);
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(UART_PORT, TX_PIN, RX_PIN, DE_PIN, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_driver_install(UART_PORT, 256, 256, 0, NULL, 0);
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_mode(UART_PORT, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "uart_set_mode RS485_HALF_DUPLEX failed: %s", esp_err_to_name(err));
        return err;
    }

    LOGGER_LOG_INFO(TAG, "RS485 UART%d ready", (int)UART_PORT);
    return ESP_OK;
}

/* =========================================================================
 *  Low-level MODBUS RTU read-holding-registers (function code 0x03)
 * ========================================================================= */

/**
 * @brief Send a MODBUS "Read Holding Registers" request and return data bytes.
 *
 * @param slave_addr   MODBUS slave address (1-247)
 * @param start_reg    Starting register address
 * @param num_regs     Number of 16-bit registers to read (1-125)
 * @param data_out     Buffer to receive the register data (num_regs × 2 bytes)
 * @param data_out_sz  Size of data_out buffer
 * @return ESP_OK on success
 */
static esp_err_t modbus_read_holding_registers(uint8_t slave_addr,
                                               uint16_t start_reg,
                                               uint16_t num_regs,
                                               uint8_t *data_out,
                                               size_t data_out_sz)
{
    /* ── Build request frame ──────────────────────────────────────────── */
    uint8_t req[8];
    req[0] = slave_addr;
    req[1] = 0x03;                          /* Read Holding Registers      */
    req[2] = (start_reg >> 8) & 0xFF;
    req[3] = start_reg & 0xFF;
    req[4] = (num_regs >> 8) & 0xFF;
    req[5] = num_regs & 0xFF;
    uint16_t crc = modbus_crc16(req, 6);
    req[6] = crc & 0xFF;                    /* CRC low byte first          */
    req[7] = (crc >> 8) & 0xFF;

    LOGGER_LOG_INFO(TAG, "TX → [%02X %02X %02X %02X %02X %02X %02X %02X]  "
                          "(slave=%d reg=%d×%d)",
                     req[0], req[1], req[2], req[3],
                     req[4], req[5], req[6], req[7],
                     slave_addr, start_reg, num_regs);

    /* ── Flush stale RX data ──────────────────────────────────────────── */
    uart_flush_input(UART_PORT);

    /* ── Transmit ─────────────────────────────────────────────────────── */
    int sent = uart_write_bytes(UART_PORT, req, sizeof(req));
    if (sent != (int)sizeof(req)) {
        LOGGER_LOG_ERROR(TAG, "uart_write_bytes: sent %d / %d", sent, (int)sizeof(req));
        return ESP_FAIL;
    }
    esp_err_t err = uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "uart_wait_tx_done: %s", esp_err_to_name(err));
        return err;
    }

    /* ── Receive ──────────────────────────────────────────────────────── */
    /* Expected: addr(1) + func(1) + byte_count(1) + data(N*2) + crc(2) */
    size_t exp_len = 5 + (size_t)(num_regs * 2);
    uint8_t resp[64];
    if (exp_len > sizeof(resp)) {
        LOGGER_LOG_ERROR(TAG, "Expected response %d exceeds buffer", (int)exp_len);
        return ESP_ERR_NO_MEM;
    }

    int rxd = uart_read_bytes(UART_PORT, resp, exp_len,
                              pdMS_TO_TICKS(RESP_TIMEOUT_MS));

    if (rxd <= 0) {
        LOGGER_LOG_ERROR(TAG, "No response from slave %d (waited %d ms)",
                         slave_addr, RESP_TIMEOUT_MS);
        return ESP_ERR_TIMEOUT;
    }

    /* Log raw bytes */
    {
        char hex[128];
        int pos = 0;
        for (int i = 0; i < rxd && pos < (int)sizeof(hex) - 4; i++)
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", resp[i]);
        LOGGER_LOG_INFO(TAG, "RX ← [%d bytes] %s", rxd, hex);
    }

    if (rxd < (int)exp_len) {
        LOGGER_LOG_WARN(TAG, "Short response: got %d bytes, expected %d", rxd, (int)exp_len);
        return ESP_ERR_INVALID_SIZE;
    }

    /* ── Check for exception response ─────────────────────────────────── */
    if (resp[1] & 0x80) {
        LOGGER_LOG_ERROR(TAG, "MODBUS EXCEPTION from slave %d  func=0x%02X  code=0x%02X",
                         slave_addr, resp[1], resp[2]);
        return ESP_FAIL;
    }

    /* ── Validate header ──────────────────────────────────────────────── */
    if (resp[0] != slave_addr) {
        LOGGER_LOG_ERROR(TAG, "Wrong slave in response: got %d expected %d",
                         resp[0], slave_addr);
        return ESP_FAIL;
    }
    if (resp[1] != 0x03) {
        LOGGER_LOG_ERROR(TAG, "Wrong func code: got 0x%02X expected 0x03", resp[1]);
        return ESP_FAIL;
    }
    if (resp[2] != num_regs * 2) {
        LOGGER_LOG_ERROR(TAG, "Byte count mismatch: got %d expected %d",
                         resp[2], num_regs * 2);
        return ESP_FAIL;
    }

    /* ── Verify CRC ───────────────────────────────────────────────────── */
    uint16_t rx_crc  = ((uint16_t)resp[rxd - 1] << 8) | resp[rxd - 2];
    uint16_t calc    = modbus_crc16(resp, rxd - 2);
    if (rx_crc != calc) {
        LOGGER_LOG_ERROR(TAG, "CRC MISMATCH  received=0x%04X  calculated=0x%04X",
                         rx_crc, calc);
        return ESP_FAIL;
    }

    /* ── Copy data payload ────────────────────────────────────────────── */
    size_t data_len = (size_t)(num_regs * 2);
    if (data_len > data_out_sz) return ESP_ERR_NO_MEM;
    memcpy(data_out, &resp[3], data_len);

    return ESP_OK;
}

/* =========================================================================
 *  MS9024-specific read helpers
 * ========================================================================= */

/** Read a single 16-bit holding register (returns host-order uint16). */
static esp_err_t ms9024_read_uint16(uint16_t reg, uint16_t *out)
{
    uint8_t d[2];
    esp_err_t err = modbus_read_holding_registers(SLAVE_ADDR, reg, 1, d, sizeof(d));
    if (err != ESP_OK) return err;
    *out = ((uint16_t)d[0] << 8) | d[1];
    return ESP_OK;
}

/**
 * Read an IEEE-754 float from two consecutive holding registers.
 *
 * MS9024 uses CDAB (word-swapped) byte order:
 *   Register N   → low word  (bytes C, D)
 *   Register N+1 → high word (bytes A, B)
 *   Reassemble as ABCD for standard IEEE-754.
 */
static esp_err_t ms9024_read_float(uint16_t reg, float *out)
{
    uint8_t d[4];
    esp_err_t err = modbus_read_holding_registers(SLAVE_ADDR, reg, 2, d, sizeof(d));
    if (err != ESP_OK) return err;

    LOGGER_LOG_INFO(TAG, "Float raw @ reg %d: [%02X %02X %02X %02X]",
                     reg, d[0], d[1], d[2], d[3]);

    /* CDAB byte order: swap the two 16-bit words */
    uint32_t raw = ((uint32_t)d[2] << 24) | ((uint32_t)d[3] << 16) |
                   ((uint32_t)d[0] << 8)  |  (uint32_t)d[1];

    float val;
    memcpy(&val, &raw, sizeof(float));

    LOGGER_LOG_INFO(TAG, "CDAB decode: raw=0x%08lX → %.4f C", (unsigned long)raw, val);

    /* Reject NaN / Infinity */
    if (isnan(val) || isinf(val)) {
        LOGGER_LOG_WARN(TAG, "Decoded NaN/Inf from reg %d — invalid data", reg);
        return ESP_FAIL;
    }

    /* Reject -0.0 (MS9024 returns this when no measurement ready) */
    if (raw == 0x80000000U) {
        LOGGER_LOG_WARN(TAG, "Got -0.0 (0x80000000) — MS9024 has no valid reading yet");
        return ESP_FAIL;
    }

    /* Sanity range check: PT100 range is roughly -200 … +850 °C */
    if (val < -200.0f || val > 1500.0f) {
        LOGGER_LOG_WARN(TAG, "Value %.2f out of range [-200, 1500] — rejecting", val);
        return ESP_FAIL;
    }

    *out = val;
    return ESP_OK;
}

/* ── MS9024 additional register addresses for diagnostics ────────────── */
#define MS9024_REG_T2         730  /* Cold junction T2 (Float, +512)      */
#define MS9024_REG_AOUT       726  /* Analog output value (Float, +512)   */
#define MS9024_REG_IN_OFFSET  524  /* Input offset (Float, +512)          */
#define MS9024_REG_PV_RAW     216  /* PV in EXP format (no +512)          */

/* =========================================================================
 *  MODBUS Write Single Register (function code 0x06)
 * ========================================================================= */

/**
 * @brief Write a single 16-bit holding register.
 *
 * Uses MODBUS function code 06 (Write Single Register).
 * The MS9024 echoes back the exact frame on success.
 *
 * @param slave_addr  MODBUS slave address
 * @param reg         Register address
 * @param value       16-bit value to write
 * @return ESP_OK on success
 */
static esp_err_t modbus_write_single_register(uint8_t slave_addr,
                                              uint16_t reg,
                                              uint16_t value)
{
    uint8_t req[8];
    req[0] = slave_addr;
    req[1] = 0x06;                           /* Write Single Register */
    req[2] = (reg >> 8) & 0xFF;
    req[3] = reg & 0xFF;
    req[4] = (value >> 8) & 0xFF;
    req[5] = value & 0xFF;
    uint16_t crc = modbus_crc16(req, 6);
    req[6] = crc & 0xFF;
    req[7] = (crc >> 8) & 0xFF;

    LOGGER_LOG_INFO(TAG, "WRITE → [%02X %02X %02X %02X %02X %02X %02X %02X]  "
                          "(slave=%d reg=%d val=%d/0x%04X)",
                     req[0], req[1], req[2], req[3],
                     req[4], req[5], req[6], req[7],
                     slave_addr, reg, value, value);

    uart_flush_input(UART_PORT);

    int sent = uart_write_bytes(UART_PORT, req, sizeof(req));
    if (sent != (int)sizeof(req)) {
        LOGGER_LOG_ERROR(TAG, "uart_write_bytes: sent %d / %d", sent, (int)sizeof(req));
        return ESP_FAIL;
    }
    esp_err_t err = uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "uart_wait_tx_done: %s", esp_err_to_name(err));
        return err;
    }

    /* MS9024 echoes back the same 8-byte frame on success */
    uint8_t resp[8];
    int rxd = uart_read_bytes(UART_PORT, resp, sizeof(resp),
                              pdMS_TO_TICKS(RESP_TIMEOUT_MS));

    if (rxd <= 0) {
        LOGGER_LOG_ERROR(TAG, "No response to write command (waited %d ms)",
                         RESP_TIMEOUT_MS);
        return ESP_ERR_TIMEOUT;
    }

    {
        char hex[64];
        int pos = 0;
        for (int i = 0; i < rxd && pos < (int)sizeof(hex) - 4; i++)
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", resp[i]);
        LOGGER_LOG_INFO(TAG, "WRITE ← [%d bytes] %s", rxd, hex);
    }

    /* Check for exception */
    if (rxd >= 2 && (resp[1] & 0x80)) {
        LOGGER_LOG_ERROR(TAG, "MODBUS EXCEPTION on write: func=0x%02X code=0x%02X",
                         resp[1], resp[2]);
        return ESP_FAIL;
    }

    /* Verify echo matches request */
    if (rxd == 8 && memcmp(req, resp, 8) == 0) {
        LOGGER_LOG_INFO(TAG, "Write confirmed — reg %d = %d (0x%04X)",
                         reg, value, value);
        return ESP_OK;
    }

    /* CRC check on response */
    if (rxd >= 8) {
        uint16_t rx_crc  = ((uint16_t)resp[rxd - 1] << 8) | resp[rxd - 2];
        uint16_t calc    = modbus_crc16(resp, rxd - 2);
        if (rx_crc != calc) {
            LOGGER_LOG_ERROR(TAG, "Write response CRC mismatch");
            return ESP_FAIL;
        }
        /* Echo content match (ignoring CRC already verified) */
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

/**
 * @brief Write a register and verify by reading back.
 */
static esp_err_t ms9024_write_and_verify(uint16_t reg, uint16_t value,
                                         const char *name)
{
    LOGGER_LOG_INFO(TAG, "Writing %s (reg %d) = %d ...", name, reg, value);

    esp_err_t err = modbus_write_single_register(SLAVE_ADDR, reg, value);
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "Failed to write %s: %s", name, esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(100));  /* Let MS9024 process the write */

    /* Read back to verify */
    uint16_t readback = 0;
    err = ms9024_read_uint16(reg, &readback);
    if (err != ESP_OK) {
        LOGGER_LOG_WARN(TAG, "Could not read back %s after write", name);
        return err;
    }

    uint16_t rb_lsb = readback & 0xFF;
    if (rb_lsb == (value & 0xFF)) {
        LOGGER_LOG_INFO(TAG, "✓ %s verified: %d", name, rb_lsb);
        return ESP_OK;
    } else {
        LOGGER_LOG_ERROR(TAG, "✗ %s mismatch: wrote %d, read back %d",
                          name, value & 0xFF, rb_lsb);
        return ESP_FAIL;
    }
}

/* =========================================================================
 *  Configuration dump (runs once at startup)
 * ========================================================================= */
static void ms9024_log_config(void)
{
    LOGGER_LOG_INFO(TAG, "╔══════════════════════════════════════╗");
    LOGGER_LOG_INFO(TAG, "║    MS9024 CONFIGURATION READOUT      ║");
    LOGGER_LOG_INFO(TAG, "╚══════════════════════════════════════╝");

    /* ── Sensor type ──────────────────────────────────────────────────── */
    uint16_t sens = 0;
    if (ms9024_read_uint16(MS9024_REG_SENS, &sens) == ESP_OK) {
        uint8_t sv = sens & 0xFF;
        const char *name;
        switch (sv) {
            case 0:  name = "TC-J";          break;
            case 1:  name = "TC-K";          break;
            case 2:  name = "TC-S";          break;
            case 3:  name = "TC-B";          break;
            case 4:  name = "TC-T";          break;
            case 5:  name = "TC-E";          break;
            case 6:  name = "TC-N";          break;
            case 7:  name = "TC-R";          break;
            case 8:  name = "TC-C";          break;
            case 10: name = "4-20mA linear"; break;
            case 11: name = "0-20mA linear"; break;
            case 12: name = "0-1V linear";   break;
            case 13: name = "0-10V linear";  break;
            case 14: name = "Pt10 385";      break;
            case 15: name = "Pt50 385";      break;
            case 16: name = "Pt100 385";     break;
            case 17: name = "Pt200 385";     break;
            case 18: name = "Pt500 385";     break;
            case 19: name = "Pt1000 385";    break;
            case 20: name = "Pt100 392";     break;
            case 21: name = "Pt100 391";     break;
            case 22: name = "Cu100 482";     break;
            case 23: name = "Ni100 617";     break;
            case 24: name = "Ni120 672";     break;
            default: name = "UNKNOWN";       break;
        }
        LOGGER_LOG_INFO(TAG, "  SENS  (reg %3d) = %3d  →  %s", MS9024_REG_SENS, sv, name);

        if (sv != SENS_PT100_385 && sv != SENS_PT100_392 && sv != SENS_PT100_391) {
            LOGGER_LOG_WARN(TAG, "  ⚠ Sensor type is NOT Pt100!  Expected 16, 20, or 21 — got %d", sv);
        }
    } else {
        LOGGER_LOG_ERROR(TAG, "  SENS  read FAILED — is the MS9024 powered and connected?");
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    /* ── Wiring mode ──────────────────────────────────────────────────── */
    uint16_t wire = 0;
    if (ms9024_read_uint16(MS9024_REG_WIRE, &wire) == ESP_OK) {
        uint8_t wv = wire & 0xFF;
        const char *ws = (wv == 4) ? "4-wire RTD" :
                         (wv == 3) ? "3-wire RTD" :
                         (wv == 2) ? "2-wire RTD" : "UNKNOWN";
        LOGGER_LOG_INFO(TAG, "  WIRE  (reg %3d) = %3d  →  %s", MS9024_REG_WIRE, wv, ws);

        if (wv != WIRE_4WIRE) {
            LOGGER_LOG_WARN(TAG, "  ⚠ Wiring mode is NOT 4-wire!  Got %d", wv);
        }
    } else {
        LOGGER_LOG_ERROR(TAG, "  WIRE  read FAILED");
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    /* ── Input filter ─────────────────────────────────────────────────── */
    uint16_t fin = 0;
    if (ms9024_read_uint16(MS9024_REG_FIN, &fin) == ESP_OK) {
        LOGGER_LOG_INFO(TAG, "  FIN   (reg %3d) = %3d  (filter; 127=off, lower=heavier)",
                         MS9024_REG_FIN, fin & 0xFF);
    } else {
        LOGGER_LOG_WARN(TAG, "  FIN   read failed (non-critical)");
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    /* ── Firmware version ─────────────────────────────────────────────── */
    uint16_t fw = 0;
    if (ms9024_read_uint16(MS9024_REG_FIRMWARE, &fw) == ESP_OK) {
        LOGGER_LOG_INFO(TAG, "  FW    (reg %3d) = %3d", MS9024_REG_FIRMWARE, fw);
    } else {
        LOGGER_LOG_WARN(TAG, "  FW    read failed (non-critical)");
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    /* ── Celsius / Fahrenheit ─────────────────────────────────────────── */
    uint16_t cf = 0;
    if (ms9024_read_uint16(MS9024_REG_CF, &cf) == ESP_OK) {
        uint8_t cv = cf & 0xFF;
        LOGGER_LOG_INFO(TAG, "  CF    (reg %3d) = %3d  →  %s",
                         MS9024_REG_CF, cv, (cv == 0) ? "Celsius" : "Fahrenheit");
        if (cv != 0) {
            LOGGER_LOG_WARN(TAG, "  ⚠ MS9024 is outputting FAHRENHEIT, not Celsius!");
        }
    } else {
        LOGGER_LOG_WARN(TAG, "  CF    read failed (non-critical, assuming Celsius)");
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    /* ── Input offset ─────────────────────────────────────────────────── */
    float offset = 0.0f;
    if (ms9024_read_float(MS9024_REG_IN_OFFSET, &offset) == ESP_OK) {
        LOGGER_LOG_INFO(TAG, "  OFFSET(reg %3d) = %.2f C", MS9024_REG_IN_OFFSET, offset);
    } else {
        LOGGER_LOG_WARN(TAG, "  OFFSET read failed (may be zero or -0.0, OK)");
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    /* ── PV (temperature) — IEEE754 float at reg 728 ─────────────────── */
    float pv = 0.0f;
    if (ms9024_read_float(PV_REG, &pv) == ESP_OK) {
        LOGGER_LOG_INFO(TAG, "  PV    (reg %3d) = %.2f C  ← current temperature",
                         PV_REG, pv);
    } else {
        LOGGER_LOG_WARN(TAG, "  PV    (reg %3d) read failed or -0.0 (sensor may need time)",
                         PV_REG);
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    /* ── T2 cold junction temperature ─────────────────────────────────── */
    float t2 = 0.0f;
    if (ms9024_read_float(MS9024_REG_T2, &t2) == ESP_OK) {
        LOGGER_LOG_INFO(TAG, "  T2    (reg %3d) = %.2f C  ← cold junction (board temp)",
                         MS9024_REG_T2, t2);
    } else {
        LOGGER_LOG_WARN(TAG, "  T2    (reg %3d) read failed", MS9024_REG_T2);
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    /* ── AOUT register ────────────────────────────────────────────────── */
    float aout = 0.0f;
    if (ms9024_read_float(MS9024_REG_AOUT, &aout) == ESP_OK) {
        LOGGER_LOG_INFO(TAG, "  AOUT  (reg %3d) = %.2f", MS9024_REG_AOUT, aout);
    } else {
        LOGGER_LOG_WARN(TAG, "  AOUT  (reg %3d) read failed", MS9024_REG_AOUT);
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    /* ── Raw 16-bit dump of registers around PV area ──────────────────── */
    LOGGER_LOG_INFO(TAG, "  ── Raw 16-bit register dump for debugging ──");
    uint16_t regs_to_dump[] = { 214, 215, 216, 217, 218, 219, 220 };
    for (int i = 0; i < (int)(sizeof(regs_to_dump)/sizeof(regs_to_dump[0])); i++) {
        uint16_t val = 0;
        if (ms9024_read_uint16(regs_to_dump[i], &val) == ESP_OK) {
            LOGGER_LOG_INFO(TAG, "  REG %3d = 0x%04X  (%5d)", regs_to_dump[i], val, val);
        } else {
            LOGGER_LOG_WARN(TAG, "  REG %3d = FAIL", regs_to_dump[i]);
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    LOGGER_LOG_INFO(TAG, "═══════════════ end config ═══════════════");
}

/* =========================================================================
 *  Periodic temperature read task
 * ========================================================================= */
static void modbus_temp_read_task(void *arg)
{
    LOGGER_LOG_INFO(TAG, "Read task started — interval=%d ms, PV reg=%d, timeout=%d ms",
                    READ_INTERVAL, PV_REG, RESP_TIMEOUT_MS);

    /* Let the MS9024 settle after power-on */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* ── Auto-correct sensor type if it doesn't match Kconfig ─────────── */
    {
        uint16_t sens = 0;
        if (ms9024_read_uint16(MS9024_REG_SENS, &sens) == ESP_OK) {
            uint8_t sv = sens & 0xFF;
            if (sv != DESIRED_SENS) {
                LOGGER_LOG_WARN(TAG,
                    "SENS mismatch: MS9024 has %d, Kconfig wants %d — correcting",
                    sv, DESIRED_SENS);
                ms9024_write_and_verify(MS9024_REG_SENS, (uint16_t)DESIRED_SENS, "SENS");
                vTaskDelay(pdMS_TO_TICKS(500));  /* Let MS9024 reconfigure */
            } else {
                LOGGER_LOG_INFO(TAG, "SENS OK: %d (matches Kconfig)", sv);
            }
        } else {
            LOGGER_LOG_ERROR(TAG, "Cannot read SENS — skipping auto-correct");
        }
    }

    /* ── Auto-correct wiring mode if it doesn't match Kconfig ─────────── */
    {
        uint16_t wire = 0;
        if (ms9024_read_uint16(MS9024_REG_WIRE, &wire) == ESP_OK) {
            uint8_t wv = wire & 0xFF;
            if (wv != DESIRED_WIRE) {
                LOGGER_LOG_WARN(TAG,
                    "WIRE mismatch: MS9024 has %d, Kconfig wants %d — correcting",
                    wv, DESIRED_WIRE);
                ms9024_write_and_verify(MS9024_REG_WIRE, (uint16_t)DESIRED_WIRE, "WIRE");
                vTaskDelay(pdMS_TO_TICKS(500));
            } else {
                LOGGER_LOG_INFO(TAG, "WIRE OK: %d (matches Kconfig)", wv);
            }
        } else {
            LOGGER_LOG_ERROR(TAG, "Cannot read WIRE — skipping auto-correct");
        }
    }

    /* ── Reset FIN filter if out of valid range (1-127) ────────────────── */
    {
        uint16_t fin = 0;
        if (ms9024_read_uint16(MS9024_REG_FIN, &fin) == ESP_OK) {
            uint8_t fv = fin & 0xFF;
            if (fv > 127) {
                LOGGER_LOG_WARN(TAG,
                    "FIN=%d is out of range (1-127) — resetting to 127 (filter OFF)",
                    fv);
                ms9024_write_and_verify(MS9024_REG_FIN, 127, "FIN");
                vTaskDelay(pdMS_TO_TICKS(200));
            } else {
                LOGGER_LOG_INFO(TAG, "FIN OK: %d", fv);
            }
        }
    }

    /* Dump device configuration once */
    ms9024_log_config();

    int consecutive_errors = 0;
    const TickType_t interval = pdMS_TO_TICKS(READ_INTERVAL);

    while (s_running)
    {
        vTaskDelay(interval);

        float temperature = 0.0f;
        esp_err_t err = ms9024_read_float(PV_REG, &temperature);

        if (err != ESP_OK) {
            consecutive_errors++;
            LOGGER_LOG_WARN(TAG, "PV read failed (%d/%d): %s",
                            consecutive_errors, MAX_ERRORS, esp_err_to_name(err));

            if (consecutive_errors >= MAX_ERRORS) {
                LOGGER_LOG_ERROR(TAG,
                    "*** %d consecutive MODBUS failures! ***  "
                    "Check: 1) RS485 wiring (TX=%d RX=%d DE=%d)  "
                    "2) MS9024 powered (24VDC)  "
                    "3) Slave addr=%d  "
                    "4) Baud=%d",
                    MAX_ERRORS, TX_PIN, RX_PIN, DE_PIN,
                    SLAVE_ADDR, BAUD_RATE);

                furnace_error_t fe = {
                    .severity   = SEVERITY_ERROR,
                    .source     = SOURCE_TEMP_MONITOR,
                    .error_code = 0xFF,
                };
                event_manager_post_blocking(FURNACE_ERROR_EVENT,
                                            FURNACE_ERROR_EVENT_ID,
                                            &fe, sizeof(fe));
                consecutive_errors = 0;   /* reset counter, keep retrying */
            }
            continue;
        }

        /* ── Successful read ──────────────────────────────────────────── */
        if (consecutive_errors > 0) {
            LOGGER_LOG_INFO(TAG, "MODBUS recovered after %d error(s)", consecutive_errors);
            consecutive_errors = 0;
        }

        LOGGER_LOG_INFO(TAG, "Temperature = %.2f C", temperature);

        /* Post exactly the same event type that temp_processor would,
           so the coordinator + HMI receive data without code changes.   */
        temp_processor_data_t data = {
            .average_temperature = temperature,
            .valid               = true,
        };
        esp_err_t post_err = event_manager_post_blocking(
            TEMP_PROCESSOR_EVENT,
            PROCESS_TEMPERATURE_EVENT_DATA,
            &data,
            sizeof(data));

        if (post_err != ESP_OK) {
            LOGGER_LOG_WARN(TAG, "Failed to post temp event: %s", esp_err_to_name(post_err));
        }
    }

    LOGGER_LOG_INFO(TAG, "Read task exiting");
    vTaskDelete(NULL);
    s_task_handle = NULL;
}

/* =========================================================================
 *  Public API
 * ========================================================================= */

esp_err_t modbus_temp_monitor_init(void)
{
    LOGGER_LOG_INFO(TAG, "╔══════════════════════════════════════════════╗");
    LOGGER_LOG_INFO(TAG, "║  MODBUS Temperature Monitor — MS9024 init   ║");
    LOGGER_LOG_INFO(TAG, "╚══════════════════════════════════════════════╝");
    LOGGER_LOG_INFO(TAG, "  Slave address : %d", SLAVE_ADDR);
    LOGGER_LOG_INFO(TAG, "  Baud rate     : %d", BAUD_RATE);
    LOGGER_LOG_INFO(TAG, "  PV register   : %d (0x%04X)", PV_REG, PV_REG);
    LOGGER_LOG_INFO(TAG, "  UART port     : %d", (int)UART_PORT);
    LOGGER_LOG_INFO(TAG, "  TX pin        : %d", TX_PIN);
    LOGGER_LOG_INFO(TAG, "  RX pin        : %d", RX_PIN);
    LOGGER_LOG_INFO(TAG, "  DE pin        : %d", DE_PIN);
    LOGGER_LOG_INFO(TAG, "  Read interval : %d ms", READ_INTERVAL);

    /* ── Init UART in RS485 half-duplex mode ──────────────────────────── */
    esp_err_t err = init_rs485_uart();
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "RS485 UART init failed: %s — aborting", esp_err_to_name(err));
        return err;
    }

    /* ── Start periodic read task ─────────────────────────────────────── */
    s_running = true;
    BaseType_t ret = xTaskCreate(
        modbus_temp_read_task,
        "modbus_temp",
        4096,
        NULL,
        5,        /* same priority as temp processor */
        &s_task_handle);

    if (ret != pdPASS) {
        LOGGER_LOG_ERROR(TAG, "Failed to create read task");
        s_running = false;
        return ESP_FAIL;
    }

    LOGGER_LOG_INFO(TAG, "MODBUS temperature monitor READY");
    return ESP_OK;
}

esp_err_t modbus_temp_monitor_shutdown(void)
{
    LOGGER_LOG_INFO(TAG, "Shutting down MODBUS temperature monitor");
    s_running = false;
    /* Task will self-delete on next iteration */
    return ESP_OK;
}

#else  /* CONFIG_MODBUS_TEMP_ENABLED not set */

esp_err_t modbus_temp_monitor_init(void)     { return ESP_OK; }
esp_err_t modbus_temp_monitor_shutdown(void)  { return ESP_OK; }

#endif /* CONFIG_MODBUS_TEMP_ENABLED */

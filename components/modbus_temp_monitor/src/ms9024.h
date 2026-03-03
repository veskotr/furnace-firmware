/**
 * @file ms9024.h
 * @brief MS9024 RTD transmitter helpers — register I/O, config, auto-correct.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/uart.h"

/* ── MS9024 register addresses (from datasheet) ─────────────────────────── */
#define MS9024_REG_SENS       28   /* Sensor type   (LSByte, R/W)          */
#define MS9024_REG_WIRE       32   /* Wiring mode   (LSByte, R/W)          */
#define MS9024_REG_FIN        26   /* Input filter   (LSByte)              */
#define MS9024_REG_FIRMWARE   126  /* Firmware version (Int)               */
#define MS9024_REG_CF         447  /* Celsius/Fahrenheit (Coil, 0=C 1=F)  */
#define MS9024_REG_T2         730  /* Cold junction T2 (Float, +512)       */
#define MS9024_REG_AOUT       726  /* Analog output value (Float, +512)    */
#define MS9024_REG_IN_OFFSET  524  /* Input offset (Float, +512)           */
#define MS9024_REG_PV_RAW     216  /* PV in EXP format (no +512)           */

/* Sensor type IDs */
#define SENS_PT100_385   16
#define SENS_PT100_392   20
#define SENS_PT100_391   21

/* Wiring mode values */
#define WIRE_4WIRE       4
#define WIRE_3WIRE       3
#define WIRE_2WIRE       2

/**
 * @brief Read a single 16-bit holding register from the MS9024.
 */
esp_err_t ms9024_read_uint16(uart_port_t port, uint8_t slave,
                             uint16_t reg, uint16_t *out, int timeout_ms);

/**
 * @brief Read an IEEE-754 float (CDAB word-swapped) from two registers.
 */
esp_err_t ms9024_read_float(uart_port_t port, uint8_t slave,
                            uint16_t reg, float *out, int timeout_ms);

/**
 * @brief Write a register and verify by reading back.
 */
esp_err_t ms9024_write_and_verify(uart_port_t port, uint8_t slave,
                                  uint16_t reg, uint16_t value,
                                  const char *name, int timeout_ms);

/**
 * @brief Auto-correct a register if it doesn't match the desired value.
 *
 * Reads the register, and if the LSByte doesn't match @p desired, writes
 * the correct value and verifies. Skips the write if already correct.
 */
esp_err_t ms9024_auto_correct_register(uart_port_t port, uint8_t slave,
                                       uint16_t reg, uint16_t desired,
                                       const char *name, int timeout_ms);

/**
 * @brief Dump MS9024 configuration to the log (runs once at startup).
 */
void ms9024_log_config(uart_port_t port, uint8_t slave,
                       uint16_t pv_reg, int timeout_ms);

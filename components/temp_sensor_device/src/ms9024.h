/**
 * @file ms9024.h
 * @brief MS9024 RTD transmitter helpers — register I/O, config, auto-correct.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

/* ── MS9024 register addresses (from datasheet) ─────────────────────────── */
#define MS9024_REG_SENS       28   /* Sensor type   (LSByte, R/W)          */
#define MS9024_REG_WIRE       32   /* Wiring mode   (LSByte, R/W)          */
#define MS9024_REG_FIN        26   /* Input filter   (LSByte)              */
#define MS9024_REG_FIRMWARE   126  /* Firmware version (Int)               */
#define MS9024_REG_CF         447  /* Celsius/Fahrenheit (Coil, 0=C 1=F)  */
#define MS9024_REG_AOUT       726  /* Analog output value (Float, +512)    */
#define MS9024_REG_PV         728  /* Process value (Float, 2 regs)        */
#define MS9024_REG_T2         730  /* Cold junction T2 (Float, +512)       */
#define MS9024_REG_IN_OFFSET  524  /* Input offset (Float, +512)           */

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
esp_err_t ms9024_read_uint16(uint8_t slave_address, uint16_t reg, uint16_t* out);

/**
 * @brief Read an IEEE-754 float (CDAB word-swapped) from two registers.
 */
esp_err_t ms9024_read_float(uint8_t slave, uint16_t reg, float* out);

/**
 * @brief Write a register and verify by reading back.
 */
esp_err_t ms9024_write_and_verify(uint8_t slave_address, uint16_t reg, uint16_t value, const char *reg_name);

/**
 * @brief Auto-correct a register if it doesn't match the desired value.
 *
 * Reads the register, and if the LSByte doesn't match @p desired, writes
 * the correct value and verifies. Skips the write if already correct.
 */
esp_err_t ms9024_auto_correct_register(uint8_t slave_address, uint16_t reg, uint16_t desired);

/**
 * @brief Dump MS9024 configuration to the log (runs once at startup).
 */
void ms9024_log_config(uint8_t slave_address, uint16_t pv_reg);

/**
 * @brief Comprehensive register scan for diagnostics.
 *
 * Reads all relevant register ranges (config, calibration, device info)
 * and dumps them to the log. Run this on both a known-good and a suspect
 * transmitter, then compare the output to find mismatched calibration.
 */
void ms9024_diagnostic_scan(uint8_t slave);

/**
 * @brief Repair a bad MS9024 by writing known-good register values.
 *
 * Writes the correct values to registers 27, 30, and 129 as captured
 * from a known-good reference unit.
 *
 * @return ESP_OK if all writes verified successfully.
 */
esp_err_t ms9024_repair_from_good_unit(uint8_t slave);

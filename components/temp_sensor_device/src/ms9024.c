/**
 * @file ms9024.c
 * @brief MS9024 transmitter helpers — register read/write, config dump,
 *        auto-correct, and sensor-name lookup.
 */

#include "ms9024.h"
#include "modbus_master.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger_component.h"
#include "utils.h"
#include "modbus_utils.h"

static const char* TAG = "MS9024";

/* =========================================================================
 *  Sensor name lookup table
 * ========================================================================= */
static const char* sensor_name(const uint8_t id)
{
    static const struct
    {
        uint8_t id;
        const char* name;
    } tbl[] = {
        {0, "TC-J"}, {1, "TC-K"}, {2, "TC-S"},
        {3, "TC-B"}, {4, "TC-T"}, {5, "TC-E"},
        {6, "TC-N"}, {7, "TC-R"}, {8, "TC-C"},
        {10, "4-20mA linear"}, {11, "0-20mA linear"}, {12, "0-1V linear"},
        {13, "0-10V linear"}, {14, "Pt10 385"}, {15, "Pt50 385"},
        {16, "Pt100 385"}, {17, "Pt200 385"}, {18, "Pt500 385"},
        {19, "Pt1000 385"}, {20, "Pt100 392"}, {21, "Pt100 391"},
        {22, "Cu100 482"}, {23, "Ni100 617"}, {24, "Ni120 672"},
    };
    for (size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++)
    {
        if (tbl[i].id == id) return tbl[i].name;
    }
    return "UNKNOWN";
}

/* =========================================================================
 *  Register read helpers
 * ========================================================================= */
esp_err_t ms9024_read_uint16(const uint8_t slave_address,
                             const uint16_t reg, uint16_t* out)
{
    uint16_t output_buffer;
    CHECK_ERR_LOG_RET_FMT(modbus_master_read_register(slave_address, reg, &output_buffer),
                          "Failed to read uint16 from reg %d", reg);
    *out = modbus_master_swap_u16(output_buffer);
    return ESP_OK;
}

esp_err_t ms9024_read_float(uint8_t slave_address,
                            uint16_t reg, float* out)
{
    uint16_t output_buffer[2];
    CHECK_ERR_LOG_RET_FMT(modbus_master_read_registers(slave_address, reg, 2, output_buffer),
                          "Failed to read float from reg %d",
                          reg);


    uint32_t raw = 0;
    const float val = modbus_master_swap_float_cdab(output_buffer, &raw);

    LOGGER_LOG_INFO(TAG, "CDAB decode: raw=0x%08lX → %.4f C", (unsigned long)raw, val);

    /* Reject NaN / Infinity */
    if (isnan(val) || isinf(val))
    {
        LOGGER_LOG_WARN(TAG, "Decoded NaN/Inf from reg %d — invalid data", reg);
        return ESP_FAIL;
    }

    /* Reject -0.0 (MS9024 returns this when no measurement ready) */
    if (raw == 0x80000000U)
    {
        LOGGER_LOG_WARN(TAG, "Got -0.0 (0x80000000) — MS9024 has no valid reading yet");
        return ESP_FAIL;
    }

    /* Sanity range check: PT100 range is roughly -200 … +850 °C */
    if (val < -200.0f || val > 1500.0f)
    {
        LOGGER_LOG_WARN(TAG, "Value %.2f out of range [-200, 1500] — rejecting", val);
        return ESP_FAIL;
    }

    *out = val;
    return ESP_OK;
}

/* =========================================================================
 *  Register write + verify
 * ========================================================================= */
esp_err_t ms9024_write_and_verify(const uint8_t slave_address, const uint16_t reg, const uint16_t value, const char *reg_name)
{
    LOGGER_LOG_INFO(TAG, "Writing %s (reg %d) = %d ...", reg_name, reg, value);

    CHECK_ERR_LOG_RET_FMT(modbus_master_write_register(slave_address, reg, value),
                          "Failed to write %s to reg %d", reg_name, reg);

    vTaskDelay(pdMS_TO_TICKS(100));  /* Let MS9024 process the write */

    /* Read back to verify */
    uint16_t readback = 0;
    CHECK_ERR_LOG_RET_FMT(ms9024_read_uint16(slave_address, reg, &readback),
                           "Failed to read back %s for verification", reg_name);

    const uint16_t rb_lsb = readback & 0xFF;
    if (rb_lsb == (value & 0xFF)) {
        LOGGER_LOG_INFO(TAG, "✓ %s verified: %d", reg_name, rb_lsb);
        return ESP_OK;
    }

    LOGGER_LOG_ERROR(TAG, "✗ %s mismatch: wrote %d, read back %d",
                      reg_name, value & 0xFF, rb_lsb);
    return ESP_FAIL;
}

/* =========================================================================
 *  Auto-correct a register to match desired value
 * ========================================================================= */
esp_err_t ms9024_auto_correct_register(const uint8_t slave_address, const uint16_t reg, const uint16_t desired)
{
    uint16_t current = 0;
    CHECK_ERR_LOG_RET(ms9024_read_uint16(slave_address, reg, &current),
                      "Failed to read for auto-correct");

    const uint8_t cv = current & 0xFF;
    if (cv != (desired & 0xFF))
    {
        LOGGER_LOG_WARN(TAG, """Mismatch: MS9024 has %d, desired %d — correcting", cv, desired & 0xFF);
        CHECK_ERR_LOG_CALL_RET_FMT(ms9024_write_and_verify(slave_address, reg, desired, "auto-correct"),
                                   vTaskDelay(pdMS_TO_TICKS(500)),
                                   "Auto-correct failed for reg %d", reg);
    }

    LOGGER_LOG_INFO(TAG, "OK: %d (matches config)", cv);
    return ESP_OK;
}

/* =========================================================================
 *  Configuration dump (runs once at startup)
 * ========================================================================= */
void ms9024_log_config(const uint8_t slave_address,
                       const uint16_t pv_reg)
{
    LOGGER_LOG_INFO(TAG, "╔══════════════════════════════════════╗");
    LOGGER_LOG_INFO(TAG, "║    MS9024 CONFIGURATION READOUT      ║");
    LOGGER_LOG_INFO(TAG, "╚══════════════════════════════════════╝");

    /* Helper macro: read uint16, log, delay */
#define LOG_U16(label, reg_addr, extra_log)                               \
        do {                                                                  \
            uint16_t v = 0;                                                   \
            if (ms9024_read_uint16(slave_address, reg_addr, &v) == ESP_OK)  \
                 { extra_log; }                                     \
            else { LOGGER_LOG_WARN(TAG, "  " label " read failed"); }         \
            vTaskDelay(pdMS_TO_TICKS(50));                                    \
        } while (0)

    /* Sensor type */
    {
        uint16_t sens = 0;
        if (ms9024_read_uint16(slave_address, MS9024_REG_SENS, &sens) == ESP_OK)
        {
            const uint8_t sv = sens & 0xFF;
            LOGGER_LOG_INFO(TAG, "  SENS  (reg %3d) = %3d  →  %s",
                            MS9024_REG_SENS, sv, sensor_name(sv));
            if (sv != SENS_PT100_385 && sv != SENS_PT100_392 && sv != SENS_PT100_391)
            {
                LOGGER_LOG_WARN(TAG, "  ⚠ Sensor type is NOT Pt100!  Expected 16, 20, or 21 — got %d", sv);
            }
        }
        else
        {
            LOGGER_LOG_ERROR(TAG, "  SENS  read FAILED — is the MS9024 powered and connected?");
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* Wiring mode */
    LOG_U16("WIRE", MS9024_REG_WIRE, {
            const uint8_t wv = v & 0xFF;
            const char *ws = (wv == 4) ? "4-wire RTD" :
            (wv == 3) ? "3-wire RTD" :
            (wv == 2) ? "2-wire RTD" : "UNKNOWN";
            LOGGER_LOG_INFO(TAG, "  WIRE  (reg %3d) = %3d  →  %s", MS9024_REG_WIRE, wv, ws);
            if (wv != WIRE_4WIRE)
            LOGGER_LOG_WARN(TAG, "  ⚠ Wiring mode is NOT 4-wire!  Got %d", wv);
            });

    /* Input filter */
    LOG_U16("FIN", MS9024_REG_FIN, {
            LOGGER_LOG_INFO(TAG, "  FIN   (reg %3d) = %3d  (filter; 127=off, lower=heavier)",
                MS9024_REG_FIN, v & 0xFF);
            });

    /* Firmware version */
    LOG_U16("FW", MS9024_REG_FIRMWARE, {
            LOGGER_LOG_INFO(TAG, "  FW    (reg %3d) = %3d", MS9024_REG_FIRMWARE, v);
            });

    /* Celsius / Fahrenheit */
    LOG_U16("CF", MS9024_REG_CF, {
            uint8_t cv = v & 0xFF;
            LOGGER_LOG_INFO(TAG, "  CF    (reg %3d) = %3d  →  %s",
                MS9024_REG_CF, cv, (cv == 0) ? "Celsius" : "Fahrenheit");
            if (cv != 0)
            LOGGER_LOG_WARN(TAG, "  ⚠ MS9024 is outputting FAHRENHEIT, not Celsius!");
            });

#undef LOG_U16

    /* Float registers: offset, PV, T2, AOUT */
    struct
    {
        const char* label;
        uint16_t reg;
    } float_regs[] = {
        {"OFFSET", MS9024_REG_IN_OFFSET},
        {"PV", pv_reg},
        {"T2", MS9024_REG_T2},
        {"AOUT", MS9024_REG_AOUT},
    };
    for (size_t i = 0; i < sizeof(float_regs) / sizeof(float_regs[0]); i++)
    {
        float fv = 0.0f;
        if (ms9024_read_float(slave_address, float_regs[i].reg, &fv) == ESP_OK)
        {
            LOGGER_LOG_INFO(TAG, "  %-6s(reg %3d) = %.2f",
                            float_regs[i].label, float_regs[i].reg, fv);
        }
        else
        {
            LOGGER_LOG_WARN(TAG, "  %-6s(reg %3d) read failed",
                            float_regs[i].label, float_regs[i].reg);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* Raw 16-bit register dump for debugging */
    LOGGER_LOG_INFO(TAG, "  ── Raw 16-bit register dump for debugging ──");
    static const uint16_t debug_regs[] = {214, 215, 216, 217, 218, 219, 220};
    for (size_t i = 0; i < sizeof(debug_regs) / sizeof(debug_regs[0]); i++)
    {
        uint16_t val = 0;
        if (ms9024_read_uint16(slave_address, debug_regs[i], &val) == ESP_OK)
        {
            LOGGER_LOG_INFO(TAG, "  REG %3d = 0x%04X  (%5d)", debug_regs[i], val, val);
        }
        else
        {
            LOGGER_LOG_WARN(TAG, "  REG %3d = FAIL", debug_regs[i]);
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    LOGGER_LOG_INFO(TAG, "═══════════════ end config ═══════════════");
}

/* =========================================================================
 *  Comprehensive register scan — compare good vs. bad transmitter
 * ========================================================================= */
void ms9024_diagnostic_scan(uint8_t slave_address)
{
    LOGGER_LOG_INFO(TAG, "╔══════════════════════════════════════════════════╗");
    LOGGER_LOG_INFO(TAG, "║  MS9024 FULL REGISTER SCAN  (compare units)      ║");
    LOGGER_LOG_INFO(TAG, "╚══════════════════════════════════════════════════╝");

    /*
     * Scan strategy: read register ranges that contain configuration,
     * calibration/trim, and device-info values. Registers that return
     * an exception (illegal data address) are silently skipped.
     *
     * Typical MS9024 / NOVUS register layout:
     *   0 –  50  : sensor config (type, wiring, filter, decimal pt …)
     *  51 – 130  : device info, firmware, serial, cal trim
     * 440 – 460  : extra config (C/F, alarms, etc.)
     * 510 – 540  : float RAM area (offsets, cal values)
     */

    /* --- Helper: scan a contiguous range of 16-bit registers --- */
#define SCAN_RANGE(label, start, end)                                   \
        do {                                                                \
            LOGGER_LOG_INFO(TAG, "── %s (regs %d – %d) ──",               \
                            label, (int)(start), (int)(end));               \
            for (uint16_t r = (start); r <= (end); r++) {                   \
                uint16_t v = 0;                                             \
                if (ms9024_read_uint16(slave_address, r, &v)      \
                    == ESP_OK) {                                            \
                    LOGGER_LOG_INFO(TAG,                                    \
                        "  REG %3d = 0x%04X  (%5d)", r, v, v);            \
                }                                                           \
                vTaskDelay(pdMS_TO_TICKS(20));                              \
            }                                                               \
        } while (0)

    /* Config area — sensor type, wiring, filter, decimal point, etc. */
    SCAN_RANGE("CONFIG", 0, 50);

    /* Device info / calibration / trim area */
    SCAN_RANGE("CAL/INFO", 100, 130);

    /* Extra config — C/F, alarms, setpoints */
    SCAN_RANGE("EXTRA CFG", 440, 460);

    /* Float RAM area — offsets, calibration values (reading as raw u16) */
    SCAN_RANGE("FLOAT RAM", 510, 540);

#undef SCAN_RANGE

    /* Also read float-decoded values for key calibration registers */
    LOGGER_LOG_INFO(TAG, "── Float-decoded calibration registers ──");
    static const struct
    {
        const char* name;
        uint16_t reg;
    } cal_floats[] = {
        {"IN_OFFSET", 524}, /* Input offset             */
        {"REG_526", 526}, /* Possible zero-trim       */
        {"REG_528", 528}, /* Possible span-trim       */
        {"REG_530", 530}, /* Possible user cal value  */
        {"REG_532", 532}, /* Possible user cal value  */
        {"REG_534", 534}, /* Possible user cal value  */
        {"REG_536", 536}, /* Possible user cal value  */
        {"PV", 728}, /* Process value            */
        {"T2", 730}, /* Cold junction / board T  */
        {"AOUT", 726}, /* Analog output            */
    };
    for (size_t i = 0; i < sizeof(cal_floats) / sizeof(cal_floats[0]); i++)
    {
        float fv = 0.0f;
        if (ms9024_read_float(slave_address, cal_floats[i].reg, &fv)
            == ESP_OK)
        {
            LOGGER_LOG_INFO(TAG, "  %-12s (reg %3d) = %.4f",
                            cal_floats[i].name, cal_floats[i].reg, fv);
        }
        else
        {
            LOGGER_LOG_WARN(TAG, "  %-12s (reg %3d) = READ FAILED / NaN",
                            cal_floats[i].name, cal_floats[i].reg);
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    LOGGER_LOG_INFO(TAG, "═══════════════ end diagnostic scan ═══════════════");
    LOGGER_LOG_INFO(TAG, "⇒ Run this on BOTH the bad and a known-good unit,");
    LOGGER_LOG_INFO(TAG, "  then diff the output to find the mismatch.");
}

/* =========================================================================
 *  Repair a bad MS9024 — write known-good register values
 *  Values captured from a known-good reference unit.
 * ========================================================================= */
esp_err_t ms9024_repair_from_good_unit(uint8_t slave_address)
{
    LOGGER_LOG_WARN(TAG, "╔══════════════════════════════════════════════════╗");
    LOGGER_LOG_WARN(TAG, "║  MS9024 REPAIR MODE — writing good-unit values   ║");
    LOGGER_LOG_WARN(TAG, "╚══════════════════════════════════════════════════╝");

    /*
     * Register diff between bad and good unit:
     *   Reg 27:  bad=200  good=0     (likely CJC/offset/linearisation param)
     *   Reg 30:  bad=0    good=2     (unknown config parameter)
     *   Reg 129: bad=0    good=282   (unknown config/cal parameter)
     *   Reg 26:  bad=127  good=50    (FIN filter — cosmetic, not causing offset)
     */
    static const struct
    {
        uint16_t reg;
        uint16_t good_val;
        const char* name;
    } repairs[] = {
        {27, 0, "REG27 (suspected offset/CJC)"},
        {30, 2, "REG30 (config param)"},
        {129, 282, "REG129 (config/cal param)"},
    };

    int ok = 0, fail = 0;

    for (size_t i = 0; i < sizeof(repairs) / sizeof(repairs[0]); i++)
    {
        /* Read current value first */
        uint16_t current = 0;
        esp_err_t err = ms9024_read_uint16(slave_address, repairs[i].reg,
                                           &current);
        if (err != ESP_OK)
        {
            LOGGER_LOG_ERROR(TAG, "  Cannot read %s (reg %d) — skipping",
                             repairs[i].name, repairs[i].reg);
            fail++;
            continue;
        }

        if (current == repairs[i].good_val)
        {
            LOGGER_LOG_INFO(TAG, "  %s (reg %d) already correct: %d",
                            repairs[i].name, repairs[i].reg, current);
            ok++;
            continue;
        }

        LOGGER_LOG_WARN(TAG, "  %s (reg %d): current=%d, writing good=%d",
                        repairs[i].name, repairs[i].reg,
                        current, repairs[i].good_val);

        err = ms9024_write_and_verify( slave_address, repairs[i].reg,
                                      repairs[i].good_val,
                                      repairs[i].name);
        if (err == ESP_OK)
        {
            LOGGER_LOG_INFO(TAG, "  ✓ %s repaired successfully", repairs[i].name);
            ok++;
        }
        else
        {
            LOGGER_LOG_ERROR(TAG, "  ✗ %s repair FAILED", repairs[i].name);
            fail++;
        }
        vTaskDelay(pdMS_TO_TICKS(500)); /* Let MS9024 save to EEPROM */
    }

    LOGGER_LOG_WARN(TAG, "Repair complete: %d OK, %d FAILED", ok, fail);
    LOGGER_LOG_WARN(TAG, "Power-cycle the MS9024 and re-check temperature.");
    LOGGER_LOG_WARN(TAG, "REMEMBER: Disable CONFIG_MODBUS_TEMP_REPAIR_BAD_UNIT after repair!");

    return (fail == 0) ? ESP_OK : ESP_FAIL;
}

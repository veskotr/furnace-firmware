/**
 * @file transmitter_diagnostics.c
 * @brief Scan the Modbus bus for the thermocouple/RTD transmitter and dump
 *        its registers as raw u16 and as CDAB-decoded floats.
 */

#include "transmitter_diagnostics.h"

#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "sdkconfig.h"

#include "logger_component.h"
#include "modbus_master.h"
#include "modbus_utils.h"

static const char *TAG = "TX_DIAG";

/* MS9024 firmware-version register — used as a "is anything there?" probe. */
#define TX_PROBE_REG  126U

static esp_err_t read_u16(uint8_t slave_addr, uint16_t reg, uint16_t *out)
{
    return modbus_master_read_register(slave_addr, reg, out);
}

static esp_err_t read_float_cdab(uint8_t slave_addr, uint16_t reg, float *out, uint32_t *raw_out)
{
    uint16_t regs[2] = {0};
    const esp_err_t err = modbus_master_read_registers(slave_addr, reg, 2, regs);
    if (err != ESP_OK)
    {
        return err;
    }

    uint32_t raw = 0;
    const float val = modbus_master_swap_float_cdab(regs, &raw);
    if (raw_out != NULL)
    {
        *raw_out = raw;
    }
    *out = val;
    return ESP_OK;
}

static void dump_reg_range(uint8_t slave_addr, uint16_t start_reg, uint16_t end_reg)
{
    LOGGER_LOG_INFO(TAG, "── regs %u – %u ──", start_reg, end_reg);
    for (uint16_t reg = start_reg; reg <= end_reg; reg++)
    {
        uint16_t value = 0;
        const esp_err_t err = read_u16(slave_addr, reg, &value);
        if (err == ESP_OK)
        {
            LOGGER_LOG_INFO(TAG, "slave=%u reg=%3u -> 0x%04X (%u)",
                            slave_addr, reg, value, value);
        }
        else
        {
            LOGGER_LOG_WARN(TAG, "slave=%u reg=%3u -> read failed",
                            slave_addr, reg);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t transmitter_diagnostics_find(uint8_t *out_slave_addr)
{
    if (out_slave_addr == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t start = (uint8_t)CONFIG_TRANSMITTER_DIAGNOSTICS_SCAN_START_ADDR;
    const uint8_t end   = (uint8_t)CONFIG_TRANSMITTER_DIAGNOSTICS_SCAN_END_ADDR;

    LOGGER_LOG_INFO(TAG, "Scanning Modbus slaves %u..%u for transmitter (probe reg %u)",
                    start, end, TX_PROBE_REG);

    for (uint8_t addr = start; addr <= end; addr++)
    {
        uint16_t fw = 0;
        if (read_u16(addr, TX_PROBE_REG, &fw) == ESP_OK)
        {
            LOGGER_LOG_INFO(TAG, "✓ transmitter answered at slave=%u (firmware=%u)", addr, fw);
            *out_slave_addr = addr;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    LOGGER_LOG_WARN(TAG, "✗ no transmitter responded in range %u..%u", start, end);
    return ESP_ERR_NOT_FOUND;
}

void transmitter_diagnostics_dump(uint8_t slave_addr)
{
    LOGGER_LOG_INFO(TAG, "========== TRANSMITTER REGISTER DUMP: slave %u ==========", slave_addr);

    /* Same ranges the MS9024 diagnostic scan treats as useful. */
    dump_reg_range(slave_addr,   0,  50);   /* config */
    dump_reg_range(slave_addr, 100, 130);   /* device info / calibration */
    dump_reg_range(slave_addr, 440, 460);   /* extra config */
    dump_reg_range(slave_addr, 510, 540);   /* float backing registers as raw u16 */

    /* Decode key float registers. */
    static const struct
    {
        const char *name;
        uint16_t reg;
    } float_regs[] = {
        { "IN_OFFSET", 524 },
        { "REG_526",   526 },
        { "REG_528",   528 },
        { "REG_530",   530 },
        { "REG_532",   532 },
        { "REG_534",   534 },
        { "REG_536",   536 },
        { "AOUT",      726 },
        { "PV",        728 },
        { "T2",        730 },
    };

    LOGGER_LOG_INFO(TAG, "── float-decoded registers (CDAB) ──");
    for (size_t i = 0; i < sizeof(float_regs) / sizeof(float_regs[0]); i++)
    {
        float fv = 0.0f;
        uint32_t raw = 0;
        const esp_err_t err = read_float_cdab(slave_addr, float_regs[i].reg, &fv, &raw);
        if (err == ESP_OK)
        {
            if (isnan(fv) || isinf(fv))
            {
                LOGGER_LOG_WARN(TAG, "slave=%u %-10s reg=%3u -> raw=0x%08lX NaN/Inf",
                                slave_addr, float_regs[i].name, float_regs[i].reg,
                                (unsigned long)raw);
            }
            else
            {
                LOGGER_LOG_INFO(TAG, "slave=%u %-10s reg=%3u -> raw=0x%08lX %.4f",
                                slave_addr, float_regs[i].name, float_regs[i].reg,
                                (unsigned long)raw, fv);
            }
        }
        else
        {
            LOGGER_LOG_WARN(TAG, "slave=%u %-10s reg=%3u -> float read failed",
                            slave_addr, float_regs[i].name, float_regs[i].reg);
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }

    LOGGER_LOG_INFO(TAG, "========== END REGISTER DUMP: slave %u ==========", slave_addr);
}

esp_err_t transmitter_diagnostics_find_and_dump(void)
{
    uint8_t slave_addr = 0;
    const esp_err_t err = transmitter_diagnostics_find(&slave_addr);
    if (err != ESP_OK)
    {
        return err;
    }

    transmitter_diagnostics_dump(slave_addr);
    return ESP_OK;
}

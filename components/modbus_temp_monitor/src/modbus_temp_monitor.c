/**
 * @file modbus_temp_monitor.c
 * @brief MODBUS RTU temperature monitor — public API and periodic read task.
 *
 * Uses the MS9024 transmitter via RS485 to read PV (temperature) and post
 * it to the event system so coordinator and HMI receive temperature data.
 */

#include "sdkconfig.h"
#include "modbus_temp_monitor.h"
#include "modbus_rtu.h"
#include "ms9024.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
#define DESIRED_SENS    CONFIG_MODBUS_TEMP_SENSOR_TYPE
#define DESIRED_WIRE    CONFIG_MODBUS_TEMP_WIRE_MODE

/* ── State ───────────────────────────────────────────────────────────────── */
static TaskHandle_t s_task_handle = NULL;
static bool s_running = false;

/* =========================================================================
 *  Periodic temperature read task
 * ========================================================================= */
static void modbus_temp_read_task(void *arg)
{
    LOGGER_LOG_INFO(TAG, "Read task started — interval=%d ms, PV reg=%d, timeout=%d ms",
                    READ_INTERVAL, PV_REG, RESP_TIMEOUT_MS);

    /* Let the MS9024 settle after power-on */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* Auto-correct sensor type, wiring mode, and filter */
    ms9024_auto_correct_register(UART_PORT, SLAVE_ADDR,
                                 MS9024_REG_SENS, (uint16_t)DESIRED_SENS,
                                 "SENS", RESP_TIMEOUT_MS);
    ms9024_auto_correct_register(UART_PORT, SLAVE_ADDR,
                                 MS9024_REG_WIRE, (uint16_t)DESIRED_WIRE,
                                 "WIRE", RESP_TIMEOUT_MS);

    /* Reset FIN filter if out of valid range (1-127) */
    {
        uint16_t fin = 0;
        if (ms9024_read_uint16(UART_PORT, SLAVE_ADDR, MS9024_REG_FIN,
                               &fin, RESP_TIMEOUT_MS) == ESP_OK) {
            uint8_t fv = fin & 0xFF;
            if (fv > 127) {
                LOGGER_LOG_WARN(TAG, "FIN=%d out of range (1-127) — resetting to 127", fv);
                ms9024_write_and_verify(UART_PORT, SLAVE_ADDR,
                                        MS9024_REG_FIN, 127, "FIN", RESP_TIMEOUT_MS);
                vTaskDelay(pdMS_TO_TICKS(200));
            } else {
                LOGGER_LOG_INFO(TAG, "FIN OK: %d", fv);
            }
        }
    }

    /* Dump device configuration once */
    ms9024_log_config(UART_PORT, SLAVE_ADDR, PV_REG, RESP_TIMEOUT_MS);

    int consecutive_errors = 0;
    const TickType_t interval = pdMS_TO_TICKS(READ_INTERVAL);

    while (s_running)
    {
        vTaskDelay(interval);

        float temperature = 0.0f;
        esp_err_t err = ms9024_read_float(UART_PORT, SLAVE_ADDR, PV_REG,
                                          &temperature, RESP_TIMEOUT_MS);

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
                consecutive_errors = 0;
            }
            continue;
        }

        /* Successful read */
        if (consecutive_errors > 0) {
            LOGGER_LOG_INFO(TAG, "MODBUS recovered after %d error(s)", consecutive_errors);
            consecutive_errors = 0;
        }

        LOGGER_LOG_INFO(TAG, "Temperature = %.2f C", temperature);

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

    esp_err_t err = modbus_rtu_init_uart(UART_PORT, TX_PIN, RX_PIN, DE_PIN, BAUD_RATE);
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "RS485 UART init failed: %s — aborting", esp_err_to_name(err));
        return err;
    }

    s_running = true;
    BaseType_t ret = xTaskCreate(
        modbus_temp_read_task,
        "modbus_temp",
        4096,
        NULL,
        5,
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
    return ESP_OK;
}

#include "nextion_transport_internal.h"

#include "sdkconfig.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "logger_component.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

/* Nextion protocol constants (fixed by hardware — not tuneable) */
#define NEXTION_CMD_TERMINATOR       0xFF
#define NEXTION_CMD_TERMINATOR_COUNT 3

static const char *TAG = "nextion_transport";
static SemaphoreHandle_t s_uart_mutex = NULL;

void nextion_uart_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = CONFIG_NEXTION_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(CONFIG_NEXTION_UART_PORT_NUM, CONFIG_NEXTION_UART_RX_BUF_SIZE, CONFIG_NEXTION_UART_TX_BUF_SIZE, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CONFIG_NEXTION_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(CONFIG_NEXTION_UART_PORT_NUM, CONFIG_NEXTION_UART_TX_PIN, CONFIG_NEXTION_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    if (s_uart_mutex == NULL) {
        s_uart_mutex = xSemaphoreCreateRecursiveMutex();
    }

    LOGGER_LOG_INFO(TAG, "UART initialized for Nextion");
}

void nextion_send_raw(const uint8_t *data, size_t length)
{
    if (!data || length == 0) {
        return;
    }
    nextion_uart_lock();
    uart_write_bytes(CONFIG_NEXTION_UART_PORT_NUM, (const char *)data, length);
    nextion_uart_unlock();
}

void nextion_send_cmd(const char *cmd)
{
    if (!cmd) {
        return;
    }

    nextion_uart_lock();
    uart_write_bytes(CONFIG_NEXTION_UART_PORT_NUM, cmd, (size_t)strlen(cmd));

    const uint8_t terminator[NEXTION_CMD_TERMINATOR_COUNT] = {
        NEXTION_CMD_TERMINATOR,
        NEXTION_CMD_TERMINATOR,
        NEXTION_CMD_TERMINATOR,
    };
    nextion_send_raw(terminator, sizeof(terminator));
    nextion_uart_unlock();
}

void nextion_uart_lock(void)
{
    if (s_uart_mutex != NULL) {
        xSemaphoreTakeRecursive(s_uart_mutex, portMAX_DELAY);
    }
}

void nextion_uart_unlock(void)
{
    if (s_uart_mutex != NULL) {
        xSemaphoreGiveRecursive(s_uart_mutex);
    }
}

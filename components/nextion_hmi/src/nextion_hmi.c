#include "nextion_hmi.h"

#include "sdkconfig.h"
#include "hmi_coordinator_internal.h"
#include "nextion_file_reader_internal.h"
#include "nextion_storage_internal.h"
#include "nextion_transport_internal.h"

#include "driver/uart.h"
#include "logger_component.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "nextion_hmi";

// Task to read lines from Nextion UART and dispatch to event handler
static void nextion_rx_task(void *arg)
{
    uint8_t rx_byte = 0;
    char line_buf[CONFIG_NEXTION_LINE_BUF_SIZE];
    size_t line_len = 0;
    uint8_t ff_count = 0;
    uint32_t rx_bytes = 0;
    uint32_t rx_lines = 0;
    TickType_t last_log = xTaskGetTickCount();

    while (true) {
        // When file reader or storage is active, it handles UART directly - don't compete
        if (nextion_file_reader_active() || nextion_storage_active()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        int read = uart_read_bytes(CONFIG_NEXTION_UART_PORT_NUM, &rx_byte, 1, pdMS_TO_TICKS(100));
        if (read <= 0) {
            TickType_t now = xTaskGetTickCount();
            if (now - last_log >= pdMS_TO_TICKS(2000)) {
                LOGGER_LOG_INFO(TAG, "Nextion RX idle: bytes=%u lines=%u active=[file:%d storage:%d]",
                         (unsigned)rx_bytes,
                         (unsigned)rx_lines,
                         nextion_file_reader_active() ? 1 : 0,
                         nextion_storage_active() ? 1 : 0);
                last_log = now;
            }
            continue;
        }

        rx_bytes++;

        if (rx_byte == 0xFF) {
            ff_count++;
            if (ff_count >= 3) {
                ff_count = 0;
                if (line_len > 0) {
                    line_buf[line_len] = '\0';
                    LOGGER_LOG_INFO(TAG, "Nextion line: %s", line_buf);
                    hmi_coordinator_post_line(line_buf);
                    rx_lines++;
                    line_len = 0;
                }
            }
            continue;
        }

        ff_count = 0;

        if (rx_byte == '\n') {
            line_buf[line_len] = '\0';
            LOGGER_LOG_INFO(TAG, "Nextion line: %s", line_buf);
            hmi_coordinator_post_line(line_buf);
            rx_lines++;
            line_len = 0;
            continue;
        }

        if (rx_byte == '\r') {
            continue;
        }

        if (line_len + 1 < CONFIG_NEXTION_LINE_BUF_SIZE) {
            line_buf[line_len++] = (char)rx_byte;
        } else {
            LOGGER_LOG_WARN(TAG, "Nextion line buffer overflow, dropping line");
            line_len = 0;
        }

        TickType_t now = xTaskGetTickCount();
        if (now - last_log >= pdMS_TO_TICKS(2000)) {
            LOGGER_LOG_INFO(TAG, "Nextion RX: bytes=%u lines=%u active=[file:%d storage:%d]",
                     (unsigned)rx_bytes,
                     (unsigned)rx_lines,
                     nextion_file_reader_active() ? 1 : 0,
                     nextion_storage_active() ? 1 : 0);
            last_log = now;
        }
    }
}

void nextion_hmi_init(void)
{
    // Initialize NVS (required for storage)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOGGER_LOG_WARN(TAG, "NVS partition needs erase");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "NVS init failed: %s", esp_err_to_name(err));
    }

    nextion_uart_init();
    nextion_file_reader_init();

    // Start coordinator (command queue + worker task) before RX task
    hmi_coordinator_init();

    xTaskCreate(nextion_rx_task, "nextion_rx", CONFIG_NEXTION_RX_TASK_STACK_SIZE, NULL, CONFIG_NEXTION_RX_TASK_PRIORITY, NULL);

    // Post initial display setup command to the coordinator queue
    hmi_coordinator_post_cmd(HMI_CMD_INIT_DISPLAY);

    LOGGER_LOG_INFO(TAG, "Nextion HMI initialized");
}

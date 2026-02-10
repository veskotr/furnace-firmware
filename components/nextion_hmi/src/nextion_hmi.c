#include "nextion_hmi.h"

#include "config.h"
#include "nextion_events.h"
#include "nextion_file_reader.h"
#include "nextion_storage.h"
#include "nextion_transport.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "nextion_hmi";

#define NEXTION_LINE_BUF_SIZE 128

// Task to read lines from Nextion UART and dispatch to event handler
static void nextion_rx_task(void *arg)
{
    uint8_t rx_byte = 0;
    char line_buf[NEXTION_LINE_BUF_SIZE];
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

        int read = uart_read_bytes(NEXTION_UART_PORT, &rx_byte, 1, pdMS_TO_TICKS(100));
        if (read <= 0) {
            TickType_t now = xTaskGetTickCount();
            if (now - last_log >= pdMS_TO_TICKS(2000)) {
                ESP_LOGI(TAG, "Nextion RX idle: bytes=%u lines=%u active=[file:%d storage:%d]",
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
                    ESP_LOGI(TAG, "Nextion line: %s", line_buf);
                    nextion_event_handle_line(line_buf);
                    rx_lines++;
                    line_len = 0;
                }
            }
            continue;
        }

        ff_count = 0;

        if (rx_byte == '\n') {
            line_buf[line_len] = '\0';
            ESP_LOGI(TAG, "Nextion line: %s", line_buf);
            nextion_event_handle_line(line_buf);
            rx_lines++;
            line_len = 0;
            continue;
        }

        if (rx_byte == '\r') {
            continue;
        }

        if (line_len + 1 < NEXTION_LINE_BUF_SIZE) {
            line_buf[line_len++] = (char)rx_byte;
        } else {
            ESP_LOGW(TAG, "Nextion line buffer overflow, dropping line");
            line_len = 0;
        }

        TickType_t now = xTaskGetTickCount();
        if (now - last_log >= pdMS_TO_TICKS(2000)) {
            ESP_LOGI(TAG, "Nextion RX: bytes=%u lines=%u active=[file:%d storage:%d]",
                     (unsigned)rx_bytes,
                     (unsigned)rx_lines,
                     nextion_file_reader_active() ? 1 : 0,
                     nextion_storage_active() ? 1 : 0);
            last_log = now;
        }
    }
}

// pushes an initial UI state to the display after boot
static void nextion_init_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    nextion_send_cmd("page " NEXTION_PAGE_MAIN);
    vTaskDelay(pdMS_TO_TICKS(30));
    nextion_update_main_status();
    vTaskDelete(NULL);
}

void nextion_hmi_init(void)
{
    config_init();

    nextion_uart_init();
    nextion_file_reader_init();

    xTaskCreate(nextion_rx_task, "nextion_rx", 4096, NULL, 10, NULL);
    xTaskCreate(nextion_init_task, "nextion_init", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Nextion HMI initialized");
}

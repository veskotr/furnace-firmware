#include "nextion_transport_internal.h"

#include "sdkconfig.h"
#include "hmi_coordinator_internal.h"
#include "nextion_file_reader_internal.h"
#include "nextion_storage_internal.h"
#include "event_manager.h"
#include "event_registry.h"

#include "driver/uart.h"
#include "logger_component.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "nextion_rx";

static const health_monitor_data_t nextion_rx_health_data = {
    .component_id = CONFIG_NEXTION_RX_COMPONENT_ID,
    .component_name = "Nextion RX",
    .timeout_ticks = pdMS_TO_TICKS(CONFIG_NEXTION_RX_HEARTBEAT_TIMEOUT_MS),
};

static void nextion_rx_task(void *arg)
{
    (void)arg;
    uint8_t rx_byte = 0;
    char line_buf[CONFIG_NEXTION_LINE_BUF_SIZE];
    size_t line_len = 0;
    uint8_t ff_count = 0;
    bool skip_until_terminator = false;  // true after buffer overflow — discard until next 0xFF 0xFF 0xFF
    uint32_t rx_bytes = 0;
    uint32_t rx_lines = 0;
    TickType_t last_log = xTaskGetTickCount();
    TickType_t last_heartbeat = xTaskGetTickCount();

    event_manager_post_health(HEALTH_MONITOR_EVENT_REGISTER, &nextion_rx_health_data);

    while (true) {
        // When file reader or storage is active, it handles UART directly - don't compete
        if (nextion_file_reader_active() || nextion_storage_active()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Check for available data WITHOUT holding the UART lock.
        // This ensures nextion_send_cmd() from the coordinator is never
        // blocked for more than a few microseconds by the RX task.
        size_t available = 0;
        uart_get_buffered_data_len(CONFIG_NEXTION_UART_PORT_NUM, &available);

        if (available == 0) {
            // No data — yield for at least 1 tick so the IDLE task can run
            // and reset the watchdog. pdMS_TO_TICKS(5) can round to 0 at
            // 100 Hz tick rate, so use an explicit minimum of 1 tick.
            vTaskDelay(pdMS_TO_TICKS(10));
            TickType_t now = xTaskGetTickCount();
            if (now - last_log >= pdMS_TO_TICKS(20000)) {
                LOGGER_LOG_INFO(TAG, "Nextion RX idle: bytes=%u lines=%u active=[file:%d storage:%d]",
                         (unsigned)rx_bytes,
                         (unsigned)rx_lines,
                         nextion_file_reader_active() ? 1 : 0,
                         nextion_storage_active() ? 1 : 0);
                last_log = now;
            }
            if ((now - last_heartbeat) >= pdMS_TO_TICKS(2000)) {
                event_manager_post_health(HEALTH_MONITOR_EVENT_HEARTBEAT, &nextion_rx_health_data);
                last_heartbeat = now;
            }
            continue;
        }

        // Data is available — lock briefly just for the read (microseconds)
        nextion_uart_lock();
        int read = uart_read_bytes(CONFIG_NEXTION_UART_PORT_NUM, &rx_byte, 1, pdMS_TO_TICKS(10));
        nextion_uart_unlock();
        if (read <= 0) {
            continue;
        }

        rx_bytes++;

        // Nextion lines are terminated by 3x 0xFF bytes, this checks for that sequence and extracts lines accordingly. 
        if (rx_byte == 0xFF) {
            ff_count++;
            if (ff_count >= 3) {
                ff_count = 0;
                if (skip_until_terminator) {
                    // Overflowed line fully consumed — resume normal parsing
                    skip_until_terminator = false;
                    line_len = 0;
                } else if (line_len > 0) {
                    line_buf[line_len] = '\0';
                    /* Skip Nextion boot status bytes (single non-printable byte) */
                    bool printable = false;
                    for (size_t i = 0; i < line_len; ++i) {
                        if ((unsigned char)line_buf[i] >= 0x20) {
                            printable = true;
                            break;
                        }
                    }
                    if (printable) {
                        LOGGER_LOG_INFO(TAG, "Nextion line: %s", line_buf);
                        hmi_coordinator_post_line(line_buf);
                        rx_lines++;
                    } else {
                        LOGGER_LOG_DEBUG(TAG, "Nextion status byte: 0x%02X (ignored)",
                                       (unsigned char)line_buf[0]);
                    }
                    line_len = 0;
                }
            }
            continue;
        }

        ff_count = 0;

        // While skipping an overflowed line, discard all non-terminator bytes
        if (skip_until_terminator) {
            continue;
        }

        //Some events may also come with newline-terminated lines, so also check for '\n' just in case.
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
            LOGGER_LOG_WARN(TAG, "Nextion line buffer overflow (%d bytes), skipping until terminator",
                           (int)line_len);
            line_len = 0;
            skip_until_terminator = true;
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

        if ((now - last_heartbeat) >= pdMS_TO_TICKS(2000)) {
            event_manager_post_health(HEALTH_MONITOR_EVENT_HEARTBEAT, &nextion_rx_health_data);
            last_heartbeat = now;
        }
    }
}

void nextion_rx_task_start(void)
{
    xTaskCreate(nextion_rx_task, "nextion_rx",
                CONFIG_NEXTION_RX_TASK_STACK_SIZE, NULL,
                CONFIG_NEXTION_RX_TASK_PRIORITY, NULL);
}

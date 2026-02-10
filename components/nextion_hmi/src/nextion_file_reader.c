#include "nextion_file_reader.h"

#include "app_config.h"
#include "nextion_transport.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "nextion_file_reader";

static volatile bool s_file_read_active = false;

void nextion_file_reader_init(void)
{
    // Nothing to init
}

bool nextion_file_reader_active(void)
{
    return s_file_read_active;
}

void nextion_file_reader_feed(uint8_t byte)
{
    // Not used in direct UART read approach
    (void)byte;
}

bool nextion_read_file(const char *path, char *out, size_t max_len, size_t *out_len)
{
    size_t total_received = 0;

    if (!path || !out || max_len == 0) {
        return false;
    }

    ESP_LOGI(TAG, "Reading file: %s (buffer: %u)", path, (unsigned)max_len);

    // Set file read active to bypass line buffering in RX task
    s_file_read_active = true;

    // Small delay to let RX task notice and pause
    vTaskDelay(pdMS_TO_TICKS(20));

    // Clear any pending UART data
    uart_flush_input(NEXTION_UART_PORT);

    bool locked = false;
    nextion_uart_lock();
    locked = true;

    // Step 1: Get file size using rdfile with count=0
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "rdfile \"%s\",0,0,0", path);
    nextion_send_cmd(cmd);

    uint8_t size_buf[4] = {0};
    int len = uart_read_bytes(NEXTION_UART_PORT, size_buf, 4, pdMS_TO_TICKS(2000));
    
    if (len != 4) {
        ESP_LOGW(TAG, "Failed to get file size, got %d bytes", len);
        goto cleanup;
    }

    uint32_t file_size = (uint32_t)size_buf[0] |
                         ((uint32_t)size_buf[1] << 8) |
                         ((uint32_t)size_buf[2] << 16) |
                         ((uint32_t)size_buf[3] << 24);

    ESP_LOGI(TAG, "File size: %u bytes", (unsigned)file_size);

    if (file_size == 0 || file_size > max_len - 1) {
        ESP_LOGW(TAG, "File size invalid or too large: %u (max %u)", 
                 (unsigned)file_size, (unsigned)(max_len - 1));
        goto cleanup;
    }

    // Step 2: Read file in chunks (Nextion serial buffer < 1024 bytes)
    #define CHUNK_SIZE 512
    uint32_t offset = 0;

    while (offset < file_size) {
        uint32_t bytes_remaining = file_size - offset;
        uint32_t chunk = (bytes_remaining > CHUNK_SIZE) ? CHUNK_SIZE : bytes_remaining;

        // Request chunk
        snprintf(cmd, sizeof(cmd), "rdfile \"%s\",%u,%u,0", path, (unsigned)offset, (unsigned)chunk);
        nextion_send_cmd(cmd);

        // Read chunk data
        size_t chunk_received = 0;
        int timeout_ms = 2000;
        int elapsed_ms = 0;

        while (chunk_received < chunk && elapsed_ms < timeout_ms) {
            size_t available = 0;
            uart_get_buffered_data_len(NEXTION_UART_PORT, &available);

            if (available > 0) {
                size_t to_read = available;
                if (chunk_received + to_read > chunk) {
                    to_read = chunk - chunk_received;
                }

                int rd = uart_read_bytes(NEXTION_UART_PORT, 
                                         (uint8_t *)out + total_received + chunk_received,
                                         to_read, pdMS_TO_TICKS(100));
                if (rd > 0) {
                    chunk_received += rd;
                    elapsed_ms = 0;
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
                elapsed_ms += 10;
            }
        }

        if (chunk_received != chunk) {
            ESP_LOGW(TAG, "Chunk read failed at offset %u: got %u of %u",
                     (unsigned)offset, (unsigned)chunk_received, (unsigned)chunk);
            goto cleanup;
        }

        total_received += chunk_received;
        offset += chunk;

        ESP_LOGI(TAG, "Read chunk: %u/%u bytes", (unsigned)total_received, (unsigned)file_size);

        // Small delay between chunks
        vTaskDelay(pdMS_TO_TICKS(5));
    }

cleanup:
    if (locked) {
        nextion_uart_unlock();
    }
    s_file_read_active = false;
    out[total_received] = '\0';
    if (out_len) {
        *out_len = total_received;
    }

    ESP_LOGI(TAG, "File read complete: %u bytes", (unsigned)total_received);
    return (total_received > 0);
}

bool nextion_file_exists(const char *path)
{
    if (!path) {
        return false;
    }

    ESP_LOGI(TAG, "Checking file: %s", path);

    s_file_read_active = true;
    vTaskDelay(pdMS_TO_TICKS(20));
    uart_flush_input(NEXTION_UART_PORT);

    bool locked = false;
    nextion_uart_lock();
    locked = true;

    char cmd[160];
    snprintf(cmd, sizeof(cmd), "rdfile \"%s\",0,0,0", path);
    nextion_send_cmd(cmd);

    uint8_t resp_buf[8] = {0};
    int len = uart_read_bytes(NEXTION_UART_PORT, resp_buf, sizeof(resp_buf), pdMS_TO_TICKS(2000));

    s_file_read_active = false;

    ESP_LOGI(TAG, "File exists check: got %d bytes, first=0x%02X", len, len > 0 ? resp_buf[0] : 0);

    if (len < 4) {
        ESP_LOGW(TAG, "File exists check failed, got %d bytes", len);
        return false;
    }

    // Nextion returns 0x06 + error code for file not found
    // or 0x05 for invalid variable/command
    if (resp_buf[0] == 0x06 || resp_buf[0] == 0x05 || resp_buf[0] == 0x04) {
        return false;
    }

    // For successful read, first 4 bytes are the file size
    uint32_t file_size = (uint32_t)resp_buf[0] |
                         ((uint32_t)resp_buf[1] << 8) |
                         ((uint32_t)resp_buf[2] << 16) |
                         ((uint32_t)resp_buf[3] << 24);

    // If we got a valid size response, file exists
    if (locked) {
        nextion_uart_unlock();
    }
    return file_size > 0;
}

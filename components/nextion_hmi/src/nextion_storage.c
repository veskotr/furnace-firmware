#include "nextion_storage.h"

#include "app_config.h"
#include "nextion_transport.h"
#include "nextion_file_reader.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "nextion_storage";

static volatile bool s_storage_active = false;

bool nextion_storage_active(void)
{
    return s_storage_active;
}

static void set_error(char *error_msg, size_t error_len, const char *msg)
{
    if (!error_msg || error_len == 0) {
        return;
    }
    snprintf(error_msg, error_len, "%s", msg);
}

static void sanitize_filename(const char *name, char *out, size_t out_len)
{
    size_t idx = 0;
    for (size_t i = 0; name[i] != '\0' && idx + 1 < out_len; ++i) {
        char c = name[i];
        if (isalnum((unsigned char)c)) {
            out[idx++] = c;
        } else if (c == ' ') {
            out[idx++] = '_';
        }
    }
    out[idx] = '\0';
}

static bool serialize_program(const ProgramDraft *draft, char *out, size_t out_len)
{
    size_t used = 0;
    int written = snprintf(out, out_len, "name=%s\n", draft->name);
    if (written < 0 || (size_t)written >= out_len) {
        return false;
    }
    used += (size_t)written;

    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        const ProgramStage *stage = &draft->stages[i];
        if (!stage->is_set) {
            continue;
        }

        // Store delta_t as x10 integer for precision (15 = 1.5°C/min)
        written = snprintf(out + used, out_len - used, "stage=%d,t=%d,target=%d,tdelta=%d,delta_x10=%d\n",
                           i + 1,
                           stage->t_min,
                           stage->target_t_c,
                           stage->t_delta_min,
                           stage->delta_t_per_min_x10);
        if (written < 0 || (size_t)written >= (out_len - used)) {
            return false;
        }
        used += (size_t)written;
    }

    return used < PROGRAM_FILE_SIZE;
}

// twfile packet header constant
static const uint8_t TWFILE_PKT_CONST[7] = {0x3A, 0xA1, 0xBB, 0x44, 0x7F, 0xFF, 0xFE};

// Wait for Nextion response with timeout
static int wait_for_response(uint8_t *buf, size_t max_len, int timeout_ms)
{
    int elapsed = 0;
    size_t received = 0;
    
    while (elapsed < timeout_ms && received < max_len) {
        size_t available = 0;
        uart_get_buffered_data_len(NEXTION_UART_PORT, &available);
        
        if (available > 0) {
            size_t to_read = available;
            if (received + to_read > max_len) {
                to_read = max_len - received;
            }
            int rd = uart_read_bytes(NEXTION_UART_PORT, buf + received, to_read, pdMS_TO_TICKS(100));
            if (rd > 0) {
                received += rd;
                // Check for complete response
                if (received >= 4 && buf[received-1] == 0xFF && buf[received-2] == 0xFF && buf[received-3] == 0xFF) {
                    return received;
                }
                if (received >= 1 && (buf[0] == 0x05 || buf[0] == 0x04)) {
                    return received;  // Single byte ACK/NAK
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
            elapsed += 10;
        }
    }
    return received;
}

bool nextion_storage_save_program(const ProgramDraft *draft, const char *original_name, char *error_msg, size_t error_len)
{
    if (!draft || draft->name[0] == '\0') {
        set_error(error_msg, error_len, "Missing program name");
        return false;
    }

    bool locked = false;
    bool success = true;

    static char payload[PROGRAM_FILE_SIZE];
    memset(payload, 0, sizeof(payload));  // Zero-fill to pad file

    if (!serialize_program(draft, payload, sizeof(payload))) {
        set_error(error_msg, error_len, "Program too large");
        return false;
    }

    size_t payload_len = strlen(payload);
    ESP_LOGI(TAG, "Saving program, payload len=%u", (unsigned)payload_len);

    char filename[64];
    sanitize_filename(draft->name, filename, sizeof(filename));
    if (filename[0] == '\0') {
        set_error(error_msg, error_len, "Invalid program name");
        return false;
    }

    char path[96];
    snprintf(path, sizeof(path), "sd0/%s%s", filename, PROGRAM_FILE_EXTENSION);

    if (nextion_file_exists(path)) {
        bool same_file = (original_name && original_name[0] != '\0' &&
                          strcmp(draft->name, original_name) == 0);
        if (!same_file) {
            set_error(error_msg, error_len, "Program name already exists");
            return false;
        }
    }

    s_storage_active = true;
    vTaskDelay(pdMS_TO_TICKS(20));
    nextion_uart_lock();
    locked = true;

    char cmd[192];
    snprintf(cmd, sizeof(cmd), "delfile \"%s\"", path);
    nextion_send_cmd(cmd);
    vTaskDelay(pdMS_TO_TICKS(50));

    uart_flush_input(NEXTION_UART_PORT);

    snprintf(cmd, sizeof(cmd), "twfile \"sd0/%s%s\",%u", filename, PROGRAM_FILE_EXTENSION, (unsigned)payload_len);
    nextion_send_cmd(cmd);

    uint8_t resp[8];
    int resp_len = wait_for_response(resp, sizeof(resp), 2000);

    ESP_LOGI(TAG, "twfile response: %d bytes, first=0x%02X", resp_len, resp_len > 0 ? resp[0] : 0);

    if (resp_len < 1) {
        set_error(error_msg, error_len, "twfile no response");
        success = false;
        goto cleanup;
    }

    if (resp[0] == 0x06) {
        set_error(error_msg, error_len, "twfile file create failed");
        success = false;
        goto cleanup;
    }

    if (resp[0] != 0xFE) {
        set_error(error_msg, error_len, "twfile unexpected response");
        ESP_LOGW(TAG, "Expected 0xFE, got 0x%02X", resp[0]);
        success = false;
        goto cleanup;
    }

    ESP_LOGI(TAG, "twfile ready, sending packets");

#define TWFILE_MAX_DATA 512
    uint16_t pkt_id = 0;
    size_t offset = 0;

    while (offset < payload_len) {
        size_t remaining = payload_len - offset;
        size_t chunk = (remaining > TWFILE_MAX_DATA) ? TWFILE_MAX_DATA : remaining;

        uint8_t header[12];
        memcpy(header, TWFILE_PKT_CONST, 7);
        header[7] = 0x00;  // vType: no CRC
        header[8] = pkt_id & 0xFF;  // pkID low byte
        header[9] = (pkt_id >> 8) & 0xFF;  // pkID high byte
        header[10] = chunk & 0xFF;  // dataSize low byte
        header[11] = (chunk >> 8) & 0xFF;  // dataSize high byte

        nextion_send_raw(header, sizeof(header));
        nextion_send_raw((const uint8_t *)(payload + offset), chunk);

        int ack = wait_for_response(resp, 1, 500);

        if (ack < 1) {
            ESP_LOGW(TAG, "No ACK for packet %u", pkt_id);
            set_error(error_msg, error_len, "twfile packet timeout");
            success = false;
            goto cleanup;
        }

        if (resp[0] == 0x04) {
            ESP_LOGW(TAG, "NAK for packet %u, retrying", pkt_id);
            continue;
        }

        if (resp[0] == 0xFD) {
            ESP_LOGI(TAG, "Packet %u sent, transfer complete", pkt_id);
            offset = payload_len;
            break;
        }

        if (resp[0] != 0x05) {
            ESP_LOGW(TAG, "Unexpected ACK 0x%02X for packet %u", resp[0], pkt_id);
            set_error(error_msg, error_len, "twfile bad ack");
            success = false;
            goto cleanup;
        }

        offset += chunk;
        pkt_id++;

        ESP_LOGI(TAG, "Packet %u sent, %u/%u bytes", pkt_id - 1, (unsigned)offset, (unsigned)payload_len);
    }

    resp_len = wait_for_response(resp, sizeof(resp), 2000);

    if (!(resp_len >= 1 && resp[0] == 0xFD)) {
        ESP_LOGW(TAG, "twfile completion response: %d bytes, first=0x%02X", resp_len, resp_len > 0 ? resp[0] : 0);
        success = false;
    }

cleanup:
    if (locked) {
        nextion_uart_unlock();
    }
    s_storage_active = false;
    return success;
}

bool nextion_storage_delete_program(const char *name, char *error_msg, size_t error_len)
{
    if (!name || name[0] == '\0') {
        set_error(error_msg, error_len, "Missing program name");
        return false;
    }

    char filename[64];
    sanitize_filename(name, filename, sizeof(filename));
    if (filename[0] == '\0') {
        set_error(error_msg, error_len, "Invalid program name");
        return false;
    }

    char path[96];
    snprintf(path, sizeof(path), "sd0/%s%s", filename, PROGRAM_FILE_EXTENSION);

    if (!nextion_file_exists(path)) {
        set_error(error_msg, error_len, "Program not found");
        return false;
    }

    ESP_LOGI(TAG, "Deleting program: %s", path);

    bool locked = false;

    s_storage_active = true;
    vTaskDelay(pdMS_TO_TICKS(20));
    nextion_uart_lock();
    locked = true;
    uart_flush_input(NEXTION_UART_PORT);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "delfile \"%s\"", path);
    nextion_send_cmd(cmd);
    vTaskDelay(pdMS_TO_TICKS(200));

    nextion_send_cmd("progBwsr.dir=\"sd0/\"");
    nextion_send_cmd("ref progBwsr");
    vTaskDelay(pdMS_TO_TICKS(50));

    if (locked) {
        nextion_uart_unlock();
    }
    s_storage_active = false;
    return true;
}

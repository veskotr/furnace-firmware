#include "nextion_settings_handlers.h"

#include "sdkconfig.h"
#include "nextion_parse_utils.h"
#include "nextion_transport_internal.h"
#include "nextion_storage_internal.h"
#include "nextion_ui_internal.h"
#include "heating_program_models_internal.h"
#include "logger_component.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "nextion_settings";

void handle_settings_init(void)
{
    char cmd[64];

    snprintf(cmd, sizeof(cmd), "cfg_t.txt=\"%d\"", CONFIG_NEXTION_MAX_OPERATIONAL_TIME_MIN);
    nextion_send_cmd(cmd);
    snprintf(cmd, sizeof(cmd), "cfg_Tmax.txt=\"%d\"", CONFIG_NEXTION_MAX_TEMPERATURE_C);
    nextion_send_cmd(cmd);
    snprintf(cmd, sizeof(cmd), "cfg_dt.txt=\"%d\"", CONFIG_NEXTION_SENSOR_READ_FREQUENCY_SEC);
    nextion_send_cmd(cmd);

    char delta_max_buf[16];
    format_delta_x10(CONFIG_NEXTION_DELTA_T_MAX_PER_MIN_X10, delta_max_buf, sizeof(delta_max_buf));
    snprintf(cmd, sizeof(cmd), "cfg_dTmax.txt=\"%s\"", delta_max_buf);
    nextion_send_cmd(cmd);
    snprintf(cmd, sizeof(cmd), "cfg_Power.txt=\"%d\"", CONFIG_NEXTION_HEATER_POWER_KW);
    nextion_send_cmd(cmd);

    snprintf(cmd, sizeof(cmd), "ambientTemp.txt=\"%d\"", program_get_ambient_temp_c());
    nextion_send_cmd(cmd);
    snprintf(cmd, sizeof(cmd), "ambientTempH.txt=\"Ambient T (%.2f)\"",
             program_get_current_temp_f());
    nextion_send_cmd(cmd);

    /* Cooldown rate — user-editable, NVS-persisted */
    {
        int rate_x10 = program_get_cooldown_rate_x10();
        char buf[16];
        format_delta_x10(rate_x10, buf, sizeof(buf));
        snprintf(cmd, sizeof(cmd), "cooldownRate.txt=\"%s\"", buf);
        nextion_send_cmd(cmd);
    }

#ifdef CONFIG_NEXTION_HAS_WIFI
    nextion_send_cmd("vis websiteQrHead,1");
    nextion_send_cmd("vis websiteQr,1");
    nextion_send_cmd("vis wirelessCfgB,1");
#endif

#ifdef CONFIG_NEXTION_HAS_BLUETOOTH
    nextion_send_cmd("vis bluetoothCfgB,1");
#endif

    LOGGER_LOG_INFO(TAG, "Settings init sent");
}

void handle_save_settings(const char *payload)
{
    if (!payload) {
        LOGGER_LOG_ERROR(TAG, "save_settings: payload is NULL");
        return;
    }

    LOGGER_LOG_INFO(TAG, "save_settings raw payload: [%s]", payload);
    LOGGER_LOG_INFO(TAG, "save_settings payload length: %d", (int)strlen(payload));

    char buffer[256];
    strncpy(buffer, payload, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *tokens[12] = {0};
    size_t token_count = 0;
    char *cursor = buffer;
    while (cursor && token_count < 12) {
        char *comma = strchr(cursor, ',');
        if (comma) *comma = '\0';
        tokens[token_count++] = cursor;
        cursor = comma ? comma + 1 : NULL;
    }

    LOGGER_LOG_INFO(TAG, "save_settings token_count: %d", (int)token_count);
    for (size_t i = 0; i < token_count; i++) {
        LOGGER_LOG_INFO(TAG, "  token[%d]: [%s]", (int)i, tokens[i] ? tokens[i] : "NULL");
    }

    if (token_count < 12) {
        char err[64];
        snprintf(err, sizeof(err), "Missing fields: got %d, need 12", (int)token_count);
        nextion_show_error(err);
        return;
    }

    int time_dirty, date_dirty, hour, min, sec, day, month, year;
    int ambient_dirty, ambient_temp;
    int cooldown_dirty;
    if (!parse_int(tokens[0], &time_dirty) || !parse_int(tokens[1], &date_dirty) ||
        !parse_int(tokens[2], &hour)       || !parse_int(tokens[3], &min)        ||
        !parse_int(tokens[4], &sec)        || !parse_int(tokens[5], &day)        ||
        !parse_int(tokens[6], &month)      || !parse_int(tokens[7], &year)       ||
        !parse_int(tokens[8], &ambient_dirty) || !parse_int(tokens[9], &ambient_temp) ||
        !parse_int(tokens[10], &cooldown_dirty)) {
        nextion_show_error("Invalid settings data");
        return;
    }

    if (time_dirty) {
        if (hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) {
            nextion_show_error("Invalid time");
            return;
        }
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "rtc3=%d", hour);
        nextion_send_cmd(cmd);
        snprintf(cmd, sizeof(cmd), "rtc4=%d", min);
        nextion_send_cmd(cmd);
        snprintf(cmd, sizeof(cmd), "rtc5=%d", sec);
        nextion_send_cmd(cmd);
        LOGGER_LOG_INFO(TAG, "Nextion RTC time set to %02d:%02d:%02d", hour, min, sec);
    }

    if (date_dirty) {
        if (day < 1 || day > 31 || month < 1 || month > 12 || year < 2000 || year > 2099) {
            nextion_show_error("Invalid date");
            return;
        }
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "rtc0=%d", year);
        nextion_send_cmd(cmd);
        snprintf(cmd, sizeof(cmd), "rtc1=%d", month);
        nextion_send_cmd(cmd);
        snprintf(cmd, sizeof(cmd), "rtc2=%d", day);
        nextion_send_cmd(cmd);
        LOGGER_LOG_INFO(TAG, "Nextion RTC date set to %04d-%02d-%02d", year, month, day);
    }

    if (!time_dirty && !date_dirty) {
        LOGGER_LOG_INFO(TAG, "Settings saved (time/date unchanged)");
    }

    if (ambient_dirty) {
        if (ambient_temp < 0 || ambient_temp > CONFIG_NEXTION_MAX_TEMPERATURE_C) {
            nextion_show_error("Invalid ambient temperature");
            return;
        }
        program_set_ambient_temp_c(ambient_temp);
        LOGGER_LOG_INFO(TAG, "Ambient temperature set to %d", ambient_temp);
    }

    if (cooldown_dirty) {
        int rate_x10;
        if (!parse_decimal_x10(tokens[11], &rate_x10)) {
            nextion_show_error("Invalid cooldown rate");
            return;
        }
        if (rate_x10 < 0) rate_x10 = -rate_x10;
        if (rate_x10 < 1) {
            rate_x10 = 1;
        } else if (rate_x10 > CONFIG_NEXTION_DELTA_T_MAX_PER_MIN_X10) {
            rate_x10 = CONFIG_NEXTION_DELTA_T_MAX_PER_MIN_X10;
        }
        program_set_cooldown_rate_x10(rate_x10);

        char buf[16], cmd[48];
        format_delta_x10(rate_x10, buf, sizeof(buf));
        snprintf(cmd, sizeof(cmd), "cooldownRate.txt=\"%s\"", buf);
        nextion_send_cmd(cmd);
        LOGGER_LOG_INFO(TAG, "Cooldown rate set: %d x10", rate_x10);
    }

    nextion_clear_error();
}

void handle_restart(void)
{
    LOGGER_LOG_INFO(TAG, "Restart requested");

    /* Reset the Nextion display first (best-effort) */
    nextion_send_cmd("rest");
    vTaskDelay(pdMS_TO_TICKS(200));

    esp_restart();
    /* does not return */
}

/* ── Factory reset ─────────────────────────────────────────────────── */

void handle_factory_reset_request(void)
{
    LOGGER_LOG_INFO(TAG, "Factory reset requested — showing confirm dialog");

    nextion_send_cmd("confirmTxt.txt=\"Factory reset? All data will be erased.\"");
    vTaskDelay(pdMS_TO_TICKS(20));
    nextion_send_cmd("vis confirmBdy,1");
    vTaskDelay(pdMS_TO_TICKS(20));
    nextion_send_cmd("vis confirmTxt,1");
    vTaskDelay(pdMS_TO_TICKS(20));
    nextion_send_cmd("vis confirmReset,1");
    vTaskDelay(pdMS_TO_TICKS(20));
    nextion_send_cmd("vis confirmCancel,1");
}

void handle_factory_reset_confirm(void)
{
    LOGGER_LOG_INFO(TAG, "Factory reset confirmed — deleting all programs");

    /* Hide the dialog */
    nextion_send_cmd("vis confirmBdy,0");
    nextion_send_cmd("vis confirmTxt,0");
    nextion_send_cmd("vis confirmReset,0");
    nextion_send_cmd("vis confirmCancel,0");

    /* Delete all tracked programs from SD */
    int deleted = nextion_storage_delete_all_programs();
    LOGGER_LOG_INFO(TAG, "Factory reset: %d programs deleted", deleted);

    /* Erase all NVS user preferences */
    esp_err_t nvs_err = nvs_flash_erase();
    if (nvs_err == ESP_OK) {
        LOGGER_LOG_INFO(TAG, "Factory reset: NVS erased");
    } else {
        LOGGER_LOG_WARN(TAG, "Factory reset: NVS erase failed: %s",
                        esp_err_to_name(nvs_err));
    }

    /* Brief pause so the user sees the dialog close */
    vTaskDelay(pdMS_TO_TICKS(300));

    /* Reset Nextion display and reboot ESP32 */
    nextion_send_cmd("rest");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    /* does not return */
}

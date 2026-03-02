#include "nextion_settings_handlers.h"

#include "sdkconfig.h"
#include "nextion_parse_utils.h"
#include "nextion_transport_internal.h"
#include "nextion_ui_internal.h"
#include "logger_component.h"

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

    char *tokens[8] = {0};
    size_t token_count = 0;
    char *cursor = buffer;
    while (cursor && token_count < 8) {
        char *comma = strchr(cursor, ',');
        if (comma) *comma = '\0';
        tokens[token_count++] = cursor;
        cursor = comma ? comma + 1 : NULL;
    }

    LOGGER_LOG_INFO(TAG, "save_settings token_count: %d", (int)token_count);
    for (size_t i = 0; i < token_count; i++) {
        LOGGER_LOG_INFO(TAG, "  token[%d]: [%s]", (int)i, tokens[i] ? tokens[i] : "NULL");
    }

    if (token_count < 8) {
        char err[64];
        snprintf(err, sizeof(err), "Missing fields: got %d, need 8", (int)token_count);
        nextion_show_error(err);
        return;
    }

    int time_dirty, date_dirty, hour, min, sec, day, month, year;
    if (!parse_int(tokens[0], &time_dirty) || !parse_int(tokens[1], &date_dirty) ||
        !parse_int(tokens[2], &hour)       || !parse_int(tokens[3], &min)        ||
        !parse_int(tokens[4], &sec)        || !parse_int(tokens[5], &day)        ||
        !parse_int(tokens[6], &month)      || !parse_int(tokens[7], &year)) {
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

    nextion_clear_error();
}

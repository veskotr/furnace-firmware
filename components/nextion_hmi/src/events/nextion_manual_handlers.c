/*
 * nextion_manual_handlers.c — Manual heating mode and fan mode handlers
 *
 * Manual mode creates a single-stage "virtual" program and starts it
 * through the same coordinator events used by program execution.
 * The existing prog_pause / prog_stop / confirm_end buttons are used
 * to pause or end the manual run — startManual only starts.
 *
 * Temp/delta inc/dec buttons apply changes immediately: stop the
 * running profile, rebuild with updated values, and restart.
 * The graph updates once per minute with real sensor data automatically
 * (same STATUS_UPDATE path as a saved program).
 */

#include "nextion_manual_handlers.h"

#include "sdkconfig.h"
#include "nextion_events_internal.h"
#include "nextion_hmi.h"
#include "nextion_run_handlers.h"
#include "nextion_transport_internal.h"
#include "nextion_ui_internal.h"
#include "nextion_parse_utils.h"
#include "heating_program_models_internal.h"
#include "core_types.h"
#include "heating_program_validation.h"
#include "event_manager.h"
#include "event_registry.h"
#include "logger_core.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

#include "commands_dispatcher.h"

static const char *TAG = "nextion_manual";

/* ── Manual control element names ──────────────────────────────────── */

static const char *MANUAL_ELEMENTS[] = {
    "manualControlH", "manCtrlBody", "targetTempH", "targetTemp",
    "incTempB", "decTempB", "tDeltaTH", "tDeltaT",
    "incDeltaB", "decDeltaB"
};
#define MANUAL_ELEMENT_COUNT (sizeof(MANUAL_ELEMENTS) / sizeof(MANUAL_ELEMENTS[0]))

/* ── Helpers ───────────────────────────────────────────────────────── */

static void show_manual_controls(void)
{
    nextion_send_cmd("manualInit.val=1");
    char cmd[48];
    for (size_t i = 0; i < MANUAL_ELEMENT_COUNT; i++) {
        snprintf(cmd, sizeof(cmd), "vis %s,1", MANUAL_ELEMENTS[i]);
        nextion_send_cmd(cmd);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void nextion_hide_manual_controls(void)
{
    nextion_send_cmd("manualInit.val=0");
    char cmd[48];
    for (size_t i = 0; i < MANUAL_ELEMENT_COUNT; i++) {
        snprintf(cmd, sizeof(cmd), "vis %s,0", MANUAL_ELEMENTS[i]);
        nextion_send_cmd(cmd);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void update_temp_display(int temp_c)
{
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "targetTemp.txt=\"%d\"", temp_c);
    nextion_send_cmd(cmd);
}

static void update_delta_display(int delta_x10)
{
    char cmd[48];
    int whole = delta_x10 / 10;
    int frac = delta_x10 % 10;
    if (frac < 0) frac = -frac;
    snprintf(cmd, sizeof(cmd), "tDeltaT.txt=\"%d.%d\"", whole, frac);
    nextion_send_cmd(cmd);
}

/**
 * @brief Build a single-stage manual program and start via coordinator.
 *
 * Uses CONFIG_NEXTION_MAX_OPERATIONAL_TIME_MIN as the stage duration so
 * the heater runs until the user explicitly stops it.  The coordinator
 * drives PID exactly like a normal saved program.  Graph updates and
 * time-remaining all work because the coordinator sends STATUS_UPDATEs.
 */
static void start_manual_program(void)
{
    int target    = program_get_manual_target_temp_c();
    int delta_x10 = program_get_manual_delta_t_x10();
    int current   = program_get_current_temp_c();
    int time_min  = CONFIG_NEXTION_MAX_OPERATIONAL_TIME_MIN;

    /* Signed delta for direction: heating = positive, cooling = negative */
    int signed_delta_x10 = (target > current) ? delta_x10 : -delta_x10;
    if (target == current) signed_delta_x10 = 0;

    /* Build a one-stage draft (stage numbers are 1-based) */
    program_draft_clear();
    program_draft_set_name("Manual");
    program_draft_set_stage(
        1,                      /* stage_number (1-based!) */
        time_min,               /* t_min */
        target,                 /* target_t_c */
        time_min,               /* t_delta_min */
        signed_delta_x10,       /* delta_t_per_min_x10 */
        true, true, true, true  /* all fields set */
    );

    /* Validate temperature within limits */
    char error_msg[96] = {0};
    if (!validate_temp_in_range(target, 1, error_msg, sizeof(error_msg))) {
        LOGGER_LOG_WARN(TAG, "Manual temp validation failed: %s", error_msg);
        nextion_show_error(error_msg);
        return;
    }

    program_draft_t program;
    hmi_get_run_program(&program);

    coordinator_command_data_t data = {
        .type = COMMAND_TYPE_COORDINATOR_START_PROFILE,
        .program = program,
        .cooldown_rate_x10 = program_get_cooldown_rate_x10()
    };

    command_t command = {
        .target = COMMAND_TARGET_COORDINATOR,
        .data = &data,
        .data_size = sizeof(coordinator_command_data_t),
    };

    esp_err_t err = commands_dispatcher_dispatch_command(&command);

    if (err != ESP_OK) {
        nextion_show_error("Manual start failed");
        LOGGER_LOG_ERROR(TAG, "Manual start event post failed: %s",
                         esp_err_to_name(err));
    }
}

/**
 * @brief Helper: if manual mode is running, post a live target update.
 *
 * Sends COORDINATOR_EVENT_UPDATE_MANUAL_TARGET so the profile task
 * picks up the new values on the next PID tick — no stop/restart,
 * no heater interruption, fully thread-safe.
 */
static void live_update_if_running(void)
{
    if (program_get_manual_mode_active() && nextion_is_profile_running()) {

        coordinator_command_data_t data = {
            .type = COMMAND_TYPE_UPDATE_MANUAL_TARGET,
            .target_t_c          = program_get_manual_target_temp_c(),
            .delta_t_per_min_x10 = program_get_manual_delta_t_x10(),
        };

        command_t command = {
            .target = COMMAND_TARGET_COORDINATOR,
            .data = &data,
            .data_size = sizeof(coordinator_command_data_t),
        };

        const esp_err_t err = commands_dispatcher_dispatch_command(&command);
        if (err != ESP_OK) {
            LOGGER_LOG_ERROR(TAG, "Live target update failed: %s",
                             esp_err_to_name(err));
        }
    }
}

/* ── Public handlers (called from router) ──────────────────────────── */

/**
 * startManual button handler.
 *
 * Works exactly like prog_start: shows manual controls, builds a
 * single-stage program running for max operational time, and posts
 * START_PROFILE.  The user uses prog_pause / prog_stop to control it.
 */
void handle_manual_toggle(void)
{
    /* Block if any profile is already running */
    if (nextion_is_profile_running()) {
        nextion_show_error("Stop program first");
        return;
    }

    program_set_manual_mode_active(true);

    int target    = program_get_manual_target_temp_c();
    int delta_x10 = program_get_manual_delta_t_x10();

    show_manual_controls();
    update_temp_display(target);
    update_delta_display(delta_x10);

    start_manual_program();
    LOGGER_LOG_INFO(TAG, "Manual mode started: target=%d delta_x10=%d",
                    target, delta_x10);
}

void handle_manual_temp_inc(void)
{
    int temp = program_get_manual_target_temp_c();
    int new_temp = temp + 5;

    if (new_temp > CONFIG_NEXTION_MAX_TEMPERATURE_C) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Max temperature is %d C",
                 CONFIG_NEXTION_MAX_TEMPERATURE_C);
        nextion_show_error(msg);
        return;
    }

    program_set_manual_target_temp_c(new_temp);
    update_temp_display(new_temp);
    LOGGER_LOG_INFO(TAG, "Manual temp inc: %d -> %d", temp, new_temp);
    live_update_if_running();
}

void handle_manual_temp_dec(void)
{
    int temp = program_get_manual_target_temp_c();
    int new_temp = temp - 5;

    if (new_temp < CONFIG_NEXTION_MIN_TEMPERATURE_C) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Min temperature is %d C",
                 CONFIG_NEXTION_MIN_TEMPERATURE_C);
        nextion_show_error(msg);
        return;
    }

    program_set_manual_target_temp_c(new_temp);
    update_temp_display(new_temp);
    LOGGER_LOG_INFO(TAG, "Manual temp dec: %d -> %d", temp, new_temp);
    live_update_if_running();
}

void handle_manual_delta_inc(void)
{
    int delta_x10 = program_get_manual_delta_t_x10();
    int new_delta = delta_x10 + 1;  /* +0.1 C/min */

    if (new_delta > CONFIG_NEXTION_DELTA_T_MAX_PER_MIN_X10) {
        char msg[64];
        char buf[16];
        format_x10_value(CONFIG_NEXTION_DELTA_T_MAX_PER_MIN_X10, buf, sizeof(buf));
        snprintf(msg, sizeof(msg), "Max delta is %s C/min", buf);
        nextion_show_error(msg);
        return;
    }

    program_set_manual_delta_t_x10(new_delta);
    update_delta_display(new_delta);
    LOGGER_LOG_INFO(TAG, "Manual delta inc: %d -> %d (x10)", delta_x10, new_delta);
    live_update_if_running();
}

void handle_manual_delta_dec(void)
{
    int delta_x10 = program_get_manual_delta_t_x10();
    int new_delta = delta_x10 - 1;  /* -0.1 C/min */

    if (new_delta < 1) {  /* minimum 0.1 C/min */
        nextion_show_error("Min delta is 0.1 C/min");
        return;
    }

    program_set_manual_delta_t_x10(new_delta);
    update_delta_display(new_delta);
    LOGGER_LOG_INFO(TAG, "Manual delta dec: %d -> %d (x10)", delta_x10, new_delta);
    live_update_if_running();
}

void handle_fan_mode_toggle(void)
{
    bool was_max = program_get_fan_mode_max();
    bool now_max = !was_max;
    program_set_fan_mode_max(now_max);

    nextion_send_cmd(now_max ? "fanMode.txt=\"Max\"" : "fanMode.txt=\"Silent\"");
    LOGGER_LOG_INFO(TAG, "Fan mode toggled: %s", now_max ? "Max" : "Silent");

    /* TODO: Post event to coordinator/fan controller when fan HW is integrated */
}

/* ── Keyboard-confirmed value handlers ─────────────────────────────── */

void handle_manual_temp_set(const char *value)
{
    if (!value || value[0] == '\0') return;

    int temp;
    if (!parse_int(value, &temp)) {
        nextion_show_error("Invalid temperature");
        update_temp_display(program_get_manual_target_temp_c());
        return;
    }

    /* Clamp to [min, max] */
    if (temp < CONFIG_NEXTION_MIN_TEMPERATURE_C) {
        temp = CONFIG_NEXTION_MIN_TEMPERATURE_C;
    } else if (temp > CONFIG_NEXTION_MAX_TEMPERATURE_C) {
        temp = CONFIG_NEXTION_MAX_TEMPERATURE_C;
    }

    program_set_manual_target_temp_c(temp);
    update_temp_display(temp);
    LOGGER_LOG_INFO(TAG, "Manual temp set (keyboard): %d", temp);
    live_update_if_running();
}

void handle_manual_delta_set(const char *value)
{
    if (!value || value[0] == '\0') return;

    int delta_x10;
    if (!parse_decimal_x10(value, &delta_x10)) {
        nextion_show_error("Invalid delta");
        update_delta_display(program_get_manual_delta_t_x10());
        return;
    }

    /* Delta must be positive (direction is computed at start) */
    if (delta_x10 < 0) delta_x10 = -delta_x10;

    /* Clamp to [0.1, max] */
    if (delta_x10 < 1) {
        delta_x10 = 1;
    } else if (delta_x10 > CONFIG_NEXTION_DELTA_T_MAX_PER_MIN_X10) {
        delta_x10 = CONFIG_NEXTION_DELTA_T_MAX_PER_MIN_X10;
    }

    program_set_manual_delta_t_x10(delta_x10);
    update_delta_display(delta_x10);
    LOGGER_LOG_INFO(TAG, "Manual delta set (keyboard): %d x10", delta_x10);
    live_update_if_running();
}

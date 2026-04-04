/*
 * nextion_events.c — Line router and display init
 *
 * This file is the thin dispatch layer: it receives cleaned Nextion
 * lines from the coordinator queue and routes them to the appropriate
 * domain handler module.  All heavy logic lives in the sub-modules:
 *
 *   nextion_program_handlers.c  — program editing / save / graph / autofill
 *   nextion_settings_handlers.c — settings page
 *   nextion_run_handlers.c      — profile run/pause/stop + live display
 *   nextion_parse_utils.c       — shared parsing helpers
 */

#include "nextion_events_internal.h"

#include "sdkconfig.h"
#include "nextion_program_handlers.h"
#include "nextion_settings_handlers.h"
#include "nextion_run_handlers.h"
#include "nextion_manual_handlers.h"
#include "nextion_transport_internal.h"
#include "nextion_ui_internal.h"
#include "heating_program_models_internal.h"
#include "logger_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "nextion_events";

/* ── Page tracking ─────────────────────────────────────────────────── */

static nextion_page_id_t s_current_page = NEXTION_PAGE_ID_MAIN;

nextion_page_id_t nextion_get_current_page(void) { return s_current_page; }
void nextion_set_current_page(nextion_page_id_t page) { s_current_page = page; }

/* ── Helpers ───────────────────────────────────────────────────────── */

static void update_main_status(void)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "currentTemp.txt=\"%.2f\"", program_get_current_temp_f());
    nextion_send_cmd(cmd);
    snprintf(cmd, sizeof(cmd), "currentKw.txt=\"%d\"", program_get_current_kw());
    nextion_send_cmd(cmd);
}

static void handle_nav_event(const char *destination)
{
    if (strcmp(destination, "programs") == 0) {
        s_current_page = NEXTION_PAGE_ID_PROGRAMS;
        program_handlers_nav_to_programs();
        return;
    }

    if (strcmp(destination, "main") == 0) {
        s_current_page = NEXTION_PAGE_ID_MAIN;
        nextion_send_cmd("page " CONFIG_NEXTION_PAGE_MAIN);
        vTaskDelay(pdMS_TO_TICKS(30));
        update_main_status();
        return;
    }

    if (strcmp(destination, "settings") == 0) {
        s_current_page = NEXTION_PAGE_ID_SETTINGS;
        nextion_send_cmd("page " CONFIG_NEXTION_PAGE_SETTINGS);
        vTaskDelay(pdMS_TO_TICKS(50));
        handle_settings_init();
        return;
    }

    LOGGER_LOG_WARN(TAG, "Unknown nav destination: %s", destination);
}

/* ── Public API (called by coordinator) ────────────────────────────── */

void nextion_update_main_status(void)
{
    update_main_status();
}

void nextion_event_handle_init(void)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    s_current_page = NEXTION_PAGE_ID_MAIN;
    nextion_send_cmd("page " CONFIG_NEXTION_PAGE_MAIN);
    vTaskDelay(pdMS_TO_TICKS(30));
    update_main_status();

    /* Restore fan mode display from NVS-backed preference */
    nextion_send_cmd(program_get_fan_mode_max()
                     ? "fanMode.txt=\"Max\""
                     : "fanMode.txt=\"Silent\"");

    /* Show current operational time from NVS */
    {
        uint32_t op_sec = program_get_operational_time_sec();
        uint32_t oh = op_sec / 3600;
        uint32_t om = (op_sec % 3600) / 60;
        uint32_t os = op_sec % 60;
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "opTime.txt=\"%02lu:%02lu:%02lu\"",
                 (unsigned long)oh, (unsigned long)om, (unsigned long)os);
        nextion_send_cmd(cmd);
    }
}

/* ── Line router ───────────────────────────────────────────────────── */

void nextion_event_handle_line(const char *line)
{
    if (!line || line[0] == '\0') {
        return;
    }

    /* Strip non-printable characters */
    char clean[512];
    size_t idx = 0;
    for (size_t i = 0; line[i] != '\0' && idx + 1 < sizeof(clean); ++i) {
        unsigned char c = (unsigned char)line[i];
        if (c >= 32 && c <= 126) {
            clean[idx++] = (char)c;
        }
    }
    clean[idx] = '\0';

    /* ── Profile run commands (always allowed) ─────────────────── */
    const char *p;

    if (strstr(clean, "prog_start")) { handle_run_start(); return; }
    if (strstr(clean, "prog_pause")) { handle_run_pause(); return; }
    if (strstr(clean, "confirm_end")) { handle_confirm_end(); return; }
    if (strstr(clean, "prog_stop"))  { handle_run_stop();  return; }

    /* ── Manual mode & fan (always allowed) ───────────────────────── */
    if (strstr(clean, "manual_toggle"))    { handle_manual_toggle();    return; }
    if (strstr(clean, "manual_temp_inc"))  { handle_manual_temp_inc();  return; }
    if (strstr(clean, "manual_temp_dec"))  { handle_manual_temp_dec();  return; }
    if (strstr(clean, "manual_delta_inc")) { handle_manual_delta_inc(); return; }
    if (strstr(clean, "manual_delta_dec")) { handle_manual_delta_dec(); return; }
    if (strstr(clean, "fan_mode_toggle"))  { handle_fan_mode_toggle();  return; }

    /* ── Manual keyboard-confirmed values (always allowed) ────────── */
    p = strstr(clean, "manual_temp_set:");
    if (p) { handle_manual_temp_set(p + 16); return; }
    p = strstr(clean, "manual_delta_set:");
    if (p) { handle_manual_delta_set(p + 17); return; }

    /* ── UI (always allowed) ─────────────────────────────────────── */
    if (strstr(clean, "err:close")) { nextion_clear_error(); return; }

    /* ── Block everything else while a program is running ────────── */
    if (nextion_is_profile_running()) {
        nextion_show_error("Stop program first");
        return;
    }

    /* ── Navigation ─────────────────────────────────────────────── */
    const char *nav = strstr(clean, "nav:");
    if (nav) { handle_nav_event(nav + 4); return; }

    /* ── Program management ─────────────────────────────────────── */

    p = strstr(clean, "prog_select:");
    if (p) {
        LOGGER_LOG_INFO(TAG, "Program select raw: %s", p + 12);
        handle_program_select(p + 12);
        return;
    }

    p = strstr(clean, "prog_page_data:");
    if (p) { handle_prog_page_data(p + 15); return; }

    if (strstr(clean, "add_prog")) { handle_add_prog(); return; }

    p = strstr(clean, "edit_prog:");
    if (p) { handle_edit_prog(p + 10); return; }

    if (strstr(clean, "prog_page:prev")) { program_handlers_page_prev(); return; }
    if (strstr(clean, "prog_page:next")) { program_handlers_page_next(); return; }

    p = strstr(clean, "save_prog:");
    if (p) { handle_save_prog(p + 10); return; }

    p = strstr(clean, "delete_prog:");
    if (p) { handle_delete_prog(p + 12); return; }

    if (strstr(clean, "confirm_delete")) { handle_confirm_delete(); return; }

    p = strstr(clean, "prog_back:");
    if (p) {
        int dirty = atoi(p + 10);
        if (dirty) {
            handle_prog_back(p + 10);
        } else {
            s_current_page = NEXTION_PAGE_ID_MAIN;
            nextion_send_cmd("page " CONFIG_NEXTION_PAGE_MAIN);
            vTaskDelay(pdMS_TO_TICKS(30));
            update_main_status();
        }
        return;
    }

    p = strstr(clean, "show_graph:");
    if (p) { handle_show_graph(p + 11); return; }

    p = strstr(clean, "autofill:");
    if (p) { handle_autofill(p + 9); return; }

    p = strstr(clean, "prog_field:");
    if (p) { handle_prog_field_set(p + 11); return; }

    /* ── Settings ───────────────────────────────────────────────── */
    if (strstr(clean, "settings_init")) { handle_settings_init(); return; }

    p = strstr(clean, "save_settings:");
    if (p) { handle_save_settings(p + 14); return; }

    if (strstr(clean, "factory_reset_confirm")) { handle_factory_reset_confirm(); return; }
    if (strstr(clean, "factory_reset"))         { handle_factory_reset_request(); return; }
    if (strstr(clean, "restart"))               { handle_restart(); return; }

    LOGGER_LOG_INFO(TAG, "Unhandled Nextion line: %s", clean);
}

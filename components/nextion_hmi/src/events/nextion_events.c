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
#include "nextion_transport_internal.h"
#include "nextion_ui_internal.h"
#include "heating_program_models_internal.h"
#include "logger_component.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "nextion_events";

/* ── Helpers ───────────────────────────────────────────────────────── */

static void update_main_status(void)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "currentTemp.txt=\"%d\"", program_get_current_temp_c());
    nextion_send_cmd(cmd);
    snprintf(cmd, sizeof(cmd), "currentKw.txt=\"%d\"", program_get_current_kw());
    nextion_send_cmd(cmd);
}

static void handle_nav_event(const char *destination)
{
    if (strcmp(destination, "programs") == 0) {
        program_handlers_nav_to_programs();
        return;
    }

    if (strcmp(destination, "main") == 0) {
        nextion_send_cmd("page " CONFIG_NEXTION_PAGE_MAIN);
        vTaskDelay(pdMS_TO_TICKS(30));
        update_main_status();
        return;
    }

    if (strcmp(destination, "settings") == 0) {
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
    nextion_send_cmd("page " CONFIG_NEXTION_PAGE_MAIN);
    vTaskDelay(pdMS_TO_TICKS(30));
    update_main_status();
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
    if (strstr(clean, "prog_start")) { handle_run_start(); return; }
    if (strstr(clean, "prog_pause")) { handle_run_pause(); return; }
    if (strstr(clean, "confirm_end")) { handle_confirm_end(); return; }
    if (strstr(clean, "prog_stop"))  { handle_run_stop();  return; }

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
    const char *p;

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

    p = strstr(clean, "show_graph:");
    if (p) { handle_show_graph(p + 11); return; }

    p = strstr(clean, "autofill:");
    if (p) { handle_autofill(p + 9); return; }

    /* ── Settings ───────────────────────────────────────────────── */
    if (strstr(clean, "settings_init")) { handle_settings_init(); return; }

    p = strstr(clean, "save_settings:");
    if (p) { handle_save_settings(p + 14); return; }

    LOGGER_LOG_INFO(TAG, "Unhandled Nextion line: %s", clean);
}

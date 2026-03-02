#include "nextion_run_handlers.h"
#include "nextion_events_internal.h"

#include "sdkconfig.h"
#include "nextion_transport_internal.h"
#include "nextion_ui_internal.h"
#include "heating_program_models.h"
#include "heating_program_models_internal.h"
#include "heating_program_validation.h"
#include "event_manager.h"
#include "event_registry.h"
#include "logger_component.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "nextion_run";

/* ── Profile active state ───────────────────────────────────────────── */

static bool     s_profile_active    = false;

/* ── Live waveform state ───────────────────────────────────────────── */

static bool     s_waveform_active       = false;
static uint32_t s_waveform_total_ms     = 0;
static uint32_t s_waveform_x            = 0;
static uint32_t s_waveform_ms_per_pixel = 1;

/**
 * Reserve pixels at the right edge of the graph so minor timing drift
 * or a delayed final update doesn't cause the waveform to clip.
 */
#define WAVEFORM_RESERVE_PX  4
#define WAVEFORM_USABLE_WIDTH  (CONFIG_NEXTION_MAIN_GRAPH_WIDTH - WAVEFORM_RESERVE_PX)

/* ── User-initiated commands ───────────────────────────────────────── */

void handle_run_start(void)
{
    char error_msg[96] = {0};
    ProgramDraft snapshot;
    program_draft_get(&snapshot);

    LOGGER_LOG_INFO(TAG, "prog_start: name='%s' temp=%d", snapshot.name, program_get_current_temp_c());

    if (!program_validate_draft_with_temp(&snapshot, program_get_current_temp_c(),
                                         error_msg, sizeof(error_msg))) {
        LOGGER_LOG_WARN(TAG, "prog_start validation failed: %s", error_msg);
        nextion_show_error(error_msg);
        return;
    }

    program_copy_draft_to_run_slot();

    coordinator_start_profile_data_t data = {
        .profile_index = 0
    };
    esp_err_t err = event_manager_post_blocking(
        COORDINATOR_EVENT,
        COORDINATOR_EVENT_START_PROFILE,
        &data,
        sizeof(data));

    if (err != ESP_OK) {
        nextion_show_error("Start failed");
    }
}

void handle_run_pause(void)
{
    esp_err_t err = event_manager_post_blocking(
        COORDINATOR_EVENT,
        COORDINATOR_EVENT_PAUSE_PROFILE,
        NULL,
        0);

    if (err != ESP_OK) {
        nextion_show_error("Pause failed");
    }
}

void handle_run_stop(void)
{
    nextion_send_cmd("confirmTxt.txt=\"End program? Can't resume.\"");
    vTaskDelay(pdMS_TO_TICKS(20));
    nextion_send_cmd("vis confirmBdy,1");
    vTaskDelay(pdMS_TO_TICKS(20));
    nextion_send_cmd("vis confirmTxt,1");
    vTaskDelay(pdMS_TO_TICKS(20));
    nextion_send_cmd("vis confirmEnd,1");
    vTaskDelay(pdMS_TO_TICKS(20));
    nextion_send_cmd("vis confirmCancel,1");
}

void handle_confirm_end(void)
{
    nextion_send_cmd("vis confirmBdy,0");
    nextion_send_cmd("vis confirmTxt,0");
    nextion_send_cmd("vis confirmEnd,0");
    nextion_send_cmd("vis confirmCancel,0");

    esp_err_t err = event_manager_post_blocking(
        COORDINATOR_EVENT,
        COORDINATOR_EVENT_STOP_PROFILE,
        NULL,
        0);

    if (err != ESP_OK) {
        nextion_show_error("Stop failed");
    }
}

/* ── Phase 4: event-driven display handlers ────────────────────────
 *
 * Called from hmi_coordinator task (never from event-loop task).
 * Fully serialized with line handlers — safe to touch shared state.
 * ----------------------------------------------------------------- */

void nextion_event_handle_temp_update(float temperature, bool valid)
{
    if (!valid) {
        return;
    }

    int temp_c = (int)(temperature + 0.5f);
    program_set_current_temp_c(temp_c);

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "currentTemp.txt=\"%d\"", temp_c);
    nextion_send_cmd(cmd);
}

void nextion_event_handle_profile_started(void)
{
    LOGGER_LOG_INFO(TAG, "Profile started — updating display");    s_profile_active = true;    nextion_send_cmd("machineState.txt=\"Running\"");
    nextion_clear_error();

    ProgramDraft draft;
    program_draft_get(&draft);

    /* Show program name on its own field */
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "progNameDisp.txt=\"%s\"", draft.name);
    nextion_send_cmd(cmd);

    uint32_t total_min = 0;
    float last_stage_temp = 0.0f;
    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        if (draft.stages[i].is_set) {
            total_min += (uint32_t)draft.stages[i].t_min;
            last_stage_temp = (float)draft.stages[i].target_t_c;
        }
    }
    s_waveform_total_ms = total_min * 60U * 1000U;

    /* Include implicit cooldown in the graph time span */
    if (last_stage_temp > 0.0f && CONFIG_NEXTION_COOLDOWN_RATE_X10 > 0) {
        float cooldown_min = (last_stage_temp * 10.0f) / (float)CONFIG_NEXTION_COOLDOWN_RATE_X10;
        if (cooldown_min < 1.0f) cooldown_min = 1.0f;
        s_waveform_total_ms += (uint32_t)(cooldown_min * 60.0f * 1000.0f);
    }

    s_waveform_x = 0;
    s_waveform_ms_per_pixel = (s_waveform_total_ms > 0)
        ? (s_waveform_total_ms / WAVEFORM_USABLE_WIDTH)
        : 1;
    s_waveform_active = true;

    snprintf(cmd, sizeof(cmd), "cle %d,1", CONFIG_NEXTION_GRAPH_DISP_ID);
    nextion_send_cmd(cmd);
}

void nextion_event_handle_status_update(uint32_t elapsed_ms, uint32_t total_ms,
                                        float current_temp, float target_temp,
                                        float power_output)
{
    char cmd[64];

    /* ── Elapsed time (HH:MM:SS) ─────────────────────────────────── */
    uint32_t elapsed_s = elapsed_ms / 1000;
    uint32_t eh = elapsed_s / 3600;
    uint32_t em = (elapsed_s % 3600) / 60;
    uint32_t es = elapsed_s % 60;
    snprintf(cmd, sizeof(cmd), "timeElapsed.txt=\"%02lu:%02lu:%02lu\"",
             (unsigned long)eh, (unsigned long)em, (unsigned long)es);
    nextion_send_cmd(cmd);

    /* ── Remaining time (HH:MM:SS) ───────────────────────────────── */
    uint32_t remaining_ms = (total_ms > elapsed_ms) ? (total_ms - elapsed_ms) : 0;
    uint32_t remaining_s = remaining_ms / 1000;
    uint32_t rh = remaining_s / 3600;
    uint32_t rm = (remaining_s % 3600) / 60;
    uint32_t rs = remaining_s % 60;
    snprintf(cmd, sizeof(cmd), "timeRamaining.txt=\"%02lu:%02lu:%02lu\"",
             (unsigned long)rh, (unsigned long)rm, (unsigned long)rs);
    nextion_send_cmd(cmd);

    /* ── Current power in kW ─────────────────────────────────────── */
    float kw = power_output * (float)CONFIG_NEXTION_HEATER_POWER_KW;
    int kw_int  = (int)kw;
    int kw_frac = (int)((kw - (float)kw_int) * 10.0f);
    if (kw_frac < 0) kw_frac = 0;
    snprintf(cmd, sizeof(cmd), "currentKw.txt=\"%d.%d\"", kw_int, kw_frac);
    nextion_send_cmd(cmd);

    /* ── Live waveform plotting (time-proportional) ─────────────── */
    if (s_waveform_active && s_waveform_ms_per_pixel > 0) {
        uint32_t target_x = elapsed_ms / s_waveform_ms_per_pixel;
        if (target_x > (uint32_t)CONFIG_NEXTION_MAIN_GRAPH_WIDTH) {
            target_x = (uint32_t)CONFIG_NEXTION_MAIN_GRAPH_WIDTH;
        }

        /* Advance the waveform to the correct x position.
         * Fill any skipped pixels with the current value. */
        int temp_c = (int)(current_temp + 0.5f);
        int y = 0;
        if (CONFIG_NEXTION_MAX_TEMPERATURE_C > 0) {
            y = (temp_c * CONFIG_NEXTION_MAIN_GRAPH_HEIGHT)
                / CONFIG_NEXTION_MAX_TEMPERATURE_C;
        }
        if (y < 0) y = 0;
        if (y > CONFIG_NEXTION_MAIN_GRAPH_HEIGHT) y = CONFIG_NEXTION_MAIN_GRAPH_HEIGHT;

        while (s_waveform_x < target_x) {
            snprintf(cmd, sizeof(cmd), "add %d,1,%d",
                     CONFIG_NEXTION_GRAPH_DISP_ID, y);
            nextion_send_cmd(cmd);
            s_waveform_x++;
        }
    }
}

void nextion_event_handle_profile_paused(void)
{
    LOGGER_LOG_INFO(TAG, "Profile paused");
    nextion_send_cmd("machineState.txt=\"Paused\"");
}

void nextion_event_handle_profile_resumed(void)
{
    LOGGER_LOG_INFO(TAG, "Profile resumed");
    nextion_send_cmd("machineState.txt=\"Running\"");
}

void nextion_event_handle_profile_stopped(void)
{
    LOGGER_LOG_INFO(TAG, "Profile stopped");
    s_profile_active = false;
    nextion_send_cmd("machineState.txt=\"Stopped\"");
    s_waveform_active = false;
}

void nextion_event_handle_profile_completed(void)
{
    LOGGER_LOG_INFO(TAG, "Profile completed");
    s_profile_active = false;
    nextion_send_cmd("machineState.txt=\"Completed\"");
    s_waveform_active = false;

    /* Zero out time remaining and power displays */
    nextion_send_cmd("timeRamaining.txt=\"00:00:00\"");
    nextion_send_cmd("currentKw.txt=\"0.0\"");
}

static const char *coordinator_error_to_str(coordinator_error_code_t code)
{
    switch (code) {
        case COORDINATOR_ERROR_NONE:                return "Unknown error";
        case COORDINATOR_ERROR_PROFILE_NOT_PAUSED:  return "Cannot pause";
        case COORDINATOR_ERROR_PROFILE_NOT_RESUMED: return "Cannot resume";
        case COORDINATOR_ERROR_PROFILE_NOT_STOPPED: return "Cannot stop";
        case COORDINATOR_ERROR_NOT_STARTED:         return "Not started";
        default:                                    return "System error";
    }
}

void nextion_event_handle_profile_error(coordinator_error_code_t code,
                                        esp_err_t esp_err)
{
    LOGGER_LOG_ERROR(TAG, "Profile error: code=%d esp_err=%s",
                     (int)code, esp_err_to_name(esp_err));

    char msg[96];
    snprintf(msg, sizeof(msg), "%s (%s)",
             coordinator_error_to_str(code), esp_err_to_name(esp_err));
    nextion_show_error(msg);
}

bool nextion_is_profile_running(void)
{
    return s_profile_active;
}

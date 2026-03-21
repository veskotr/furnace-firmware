#include "nextion_program_handlers.h"

#include "sdkconfig.h"
#include "nextion_events_internal.h"
#include "nextion_parse_utils.h"
#include "nextion_transport_internal.h"
#include "nextion_storage_internal.h"
#include "nextion_ui_internal.h"
#include "core_types.h"
#include "heating_program_models_internal.h"
#include "heating_program_graph_internal.h"
#include "heating_program_validation.h"
#include "logger_component.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "nextion_prog";

/* ── Module state ──────────────────────────────────────────────────── */

static uint8_t s_programs_page = 1;
static bool    s_graph_visible = false;
static char    s_original_program_name[64] = {0};

/* ── Confirm-dialog context ─────────────────────────────────────────── */

typedef enum {
    DIALOG_NONE,
    DIALOG_DELETE,    // Confirm deletion of existing program
    DIALOG_CLEAR,     // Clear all fields (new program mode)
    DIALOG_EXIT,      // Exit without saving (dirty flag set)
} dialog_context_t;

static dialog_context_t s_dialog_context = DIALOG_NONE;

/* ── Common payload tokenizer ──────────────────────────────────────── */

#define PAGE_TOKEN_COUNT (1 + PROGRAMS_PAGE_STAGE_COUNT * 5)

static size_t tokenize_payload(const char *payload, char *buf, size_t buf_size,
                               char **tokens, size_t max_tokens)
{
    strncpy(buf, payload, buf_size - 1);
    buf[buf_size - 1] = '\0';

    size_t count = 0;
    char *cursor = buf;
    while (cursor && count < max_tokens) {
        char *comma = strchr(cursor, ',');
        if (comma) {
            *comma = '\0';
        }
        tokens[count++] = cursor;
        cursor = comma ? comma + 1 : NULL;
    }
    return count;
}

static bool tokenize_and_validate(const char *payload, const char *label,
                                  char *buf, size_t buf_size,
                                  char **tokens, size_t max_tokens)
{
    size_t count = tokenize_payload(payload, buf, buf_size, tokens, max_tokens);
    if (count < max_tokens) {
        char err[64];
        snprintf(err, sizeof(err), "%s: got %u fields, need %u",
                 label, (unsigned)count, (unsigned)max_tokens);
        nextion_show_error(err);
        return false;
    }
    return true;
}

/* ── Stage field parsing ────────────────────────────────────────────── */

typedef struct {
    int      t_min;
    int      target_t;
    int      t_delta;
    int      delta_t_x10;
    bool     t_set;
    bool     target_set;
    bool     t_delta_set;
    bool     delta_t_set;
    bool     any_set;
    uint8_t  stage_num;
    uint8_t  field_num;
} StageFields;

/**
 * Parse one stage's 5 tokens (bStg label + 4 value fields) from the token
 * array.  Advances *idx by 5.  Returns false if any parse call fails.
 */
static bool parse_stage_fields(char **tokens, size_t *idx, int row, StageFields *f)
{
    f->stage_num = (uint8_t)((s_programs_page - 1) * PROGRAMS_PAGE_STAGE_COUNT + row + 1);
    f->field_num = (uint8_t)(row + 1);
    f->t_min = 0;  f->target_t = 0;  f->t_delta = 0;  f->delta_t_x10 = 0;
    f->t_set = false;  f->target_set = false;  f->t_delta_set = false;  f->delta_t_set = false;

    (*idx)++;  // skip bStg label

    bool ok = parse_optional_int(tokens[(*idx)++], &f->t_min, &f->t_set)
           && parse_optional_int(tokens[(*idx)++], &f->target_t, &f->target_set)
           && parse_optional_int(tokens[(*idx)++], &f->t_delta, &f->t_delta_set)
           && parse_optional_delta_x10(tokens[(*idx)++], &f->delta_t_x10, &f->delta_t_set);

    f->any_set = f->t_set || f->target_set || f->t_delta_set || f->delta_t_set;
    return ok;
}

/* ── Nextion chunked text helper ───────────────────────────────────── */

static void nextion_set_text_chunked(const char *obj_name, const char *text)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "%s.txt=\"\"", obj_name);
    nextion_send_cmd(cmd);

    if (!text || text[0] == '\0') {
        return;
    }

    size_t len = strlen(text);
    size_t i = 0;
    int chunk_count = 0;
    while (i < len) {
        char chunk[48];
        size_t out = 0;

        while (i < len && out + 2 < sizeof(chunk)) {
            char c = text[i++];
            if (c == '\n') {
                chunk[out++] = '\\';
                chunk[out++] = 'r';
            } else if (c == '"' || c == '\\') {
                chunk[out++] = '\\';
                chunk[out++] = c;
            } else if (c == '\r') {
                continue;
            } else {
                chunk[out++] = c;
            }
        }
        chunk[out] = '\0';

        snprintf(cmd, sizeof(cmd), "%s.txt+=\"%s\"", obj_name, chunk);
        nextion_send_cmd(cmd);

        if ((++chunk_count % 8) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

/* ── Buffer serialization ──────────────────────────────────────────── */

static void sync_program_buffer(void)
{
    static char payload[CONFIG_NEXTION_PROGRAM_FILE_SIZE] = {0};
    program_draft_t draft;
    program_draft_get(&draft);
    if (!nextion_serialize_program(&draft, payload, sizeof(payload))) {
        return;
    }
    nextion_set_text_chunked("programBuffer", payload);
}

/* ── Stage-field Nextion senders ────────────────────────────────────── */

static void send_int_field(const char *prefix, uint8_t field_num,
                           int value, bool is_set)
{
    char cmd[64];
    if (is_set) {
        snprintf(cmd, sizeof(cmd), "%s%u.txt=\"%d\"", prefix, (unsigned)field_num, value);
    } else {
        snprintf(cmd, sizeof(cmd), "%s%u.txt=\"\"", prefix, (unsigned)field_num);
    }
    nextion_send_cmd(cmd);
}

static void send_delta_field(uint8_t field_num, int delta_x10, bool is_set)
{
    char cmd[64];
    if (is_set) {
        char delta_buf[16];
        format_delta_x10(delta_x10, delta_buf, sizeof(delta_buf));
        snprintf(cmd, sizeof(cmd), "tempDelta%u.txt=\"%s\"", (unsigned)field_num, delta_buf);
    } else {
        snprintf(cmd, sizeof(cmd), "tempDelta%u.txt=\"\"", (unsigned)field_num);
    }
    nextion_send_cmd(cmd);
}

/* ── Page navigation ───────────────────────────────────────────────── */

static void programs_page_apply(uint8_t page)
{
    if (page < 1) {
        page = 1;
    } else if (page > PROGRAMS_PAGE_COUNT) {
        page = PROGRAMS_PAGE_COUNT;
    }

    s_programs_page = page;

    char cmd[64];

    snprintf(cmd, sizeof(cmd), "pageNum.txt=\"%u\"", (unsigned)page);
    nextion_send_cmd(cmd);

    for (uint8_t i = 0; i < PROGRAMS_PAGE_STAGE_COUNT; ++i) {
        uint8_t stage_num = (uint8_t)((page - 1) * PROGRAMS_PAGE_STAGE_COUNT + (i + 1));
        snprintf(cmd, sizeof(cmd), "bStg%u.txt=\"%u\"", (unsigned)(i + 1), (unsigned)stage_num);
        nextion_send_cmd(cmd);
    }

    program_draft_t draft;
    program_draft_get(&draft);
    for (uint8_t i = 0; i < PROGRAMS_PAGE_STAGE_COUNT; ++i) {
        uint8_t stage_idx = (uint8_t)((page - 1) * PROGRAMS_PAGE_STAGE_COUNT + i);
        const program_stage_t *stage = &draft.stages[stage_idx];
        uint8_t field_num = i + 1;

        bool set = stage->is_set;
        send_int_field("t",          field_num, stage->t_min,               set && stage->t_set);
        send_int_field("targetTMax", field_num, stage->target_t_c,          set && stage->target_set);
        send_int_field("tDelta",     field_num, stage->t_delta_min,         set && stage->t_delta_set);
        send_delta_field(             field_num, stage->delta_t_per_min_x10, set && stage->delta_t_set);
    }
}

/* ── Public API (called from router) ───────────────────────────────── */

void program_handlers_nav_to_programs(void)
{
    s_original_program_name[0] = '\0';
    program_draft_clear();
    s_programs_page = 1;
    s_graph_visible = false;
    nextion_send_cmd("page " CONFIG_NEXTION_PAGE_PROGRAMS);
}

void program_handlers_page_prev(void)
{
    programs_page_apply((uint8_t)(s_programs_page - 1));
}

void program_handlers_page_next(void)
{
    programs_page_apply((uint8_t)(s_programs_page + 1));
}

/* ── Helper: compute starting temp for current page ────────────────── */

static int starting_temp_for_current_page(void)
{
    int current_temp = program_get_ambient_temp_c();
    program_draft_t draft;
    program_draft_get(&draft);
    for (int i = 0; i < (s_programs_page - 1) * PROGRAMS_PAGE_STAGE_COUNT; ++i) {
        if (draft.stages[i].is_set && draft.stages[i].target_set) {
            current_temp = draft.stages[i].target_t_c;
        }
    }
    return current_temp;
}

/* ── Graph rendering helper ─────────────────────────────────────────── */

static bool render_graph_to_nextion(const program_draft_t *draft, int graph_id,
                                    int width, int height)
{
    static uint8_t samples[
        (CONFIG_NEXTION_PROGRAMS_GRAPH_WIDTH > CONFIG_NEXTION_MAIN_GRAPH_WIDTH)
        ? CONFIG_NEXTION_PROGRAMS_GRAPH_WIDTH : CONFIG_NEXTION_MAIN_GRAPH_WIDTH];

    if (width <= 0 || (size_t)width > sizeof(samples)) {
        nextion_show_error("Graph: invalid width");
        return false;
    }

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "cle %d,0", graph_id);
    nextion_send_cmd(cmd);

    size_t count = program_build_graph(draft, samples, sizeof(samples), width,
                                       CONFIG_NEXTION_MAX_TEMPERATURE_C,
                                       program_get_ambient_temp_c());
    if (count == 0) {
        nextion_show_error("Graph: no data");
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        snprintf(cmd, sizeof(cmd), "add %d,0,%u", graph_id, (unsigned)samples[i]);
        nextion_send_cmd(cmd);
        if ((i % 64) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    return true;
}

/* ── handle_save_prog ──────────────────────────────────────────────── */

void handle_save_prog(const char *payload)
{
    if (!payload) {
        return;
    }

    char buffer[512];
    char *tokens[PAGE_TOKEN_COUNT] = {0};
    if (!tokenize_and_validate(payload, "Save", buffer, sizeof(buffer),
                               tokens, PAGE_TOKEN_COUNT)) {
        return;
    }

    program_draft_set_name(trim_in_place(tokens[0]));

    size_t idx = 1;
    for (int row = 0; row < PROGRAMS_PAGE_STAGE_COUNT; ++row) {
        StageFields f;
        if (!parse_stage_fields(tokens, &idx, row, &f)) {
            nextion_show_error("Invalid numeric input");
            return;
        }

        if (!f.any_set) {
            program_draft_clear_stage(f.stage_num);
            continue;
        }

        if (!f.target_set) {
            char err[48];
            snprintf(err, sizeof(err), "Stage %d: Target temp required", f.stage_num);
            nextion_show_error(err);
            return;
        }

        if (!f.t_set && !f.delta_t_set) {
            char err[64];
            snprintf(err, sizeof(err), "Stage %d: Add Time & Delta T or use Autofill", f.stage_num);
            nextion_show_error(err);
            return;
        }

        if (!f.t_set) {
            char err[64];
            snprintf(err, sizeof(err), "Stage %d: Time missing. Use Autofill", f.stage_num);
            nextion_show_error(err);
            return;
        }

        if (!f.delta_t_set) {
            char err[64];
            snprintf(err, sizeof(err), "Stage %d: Delta T missing. Use Autofill", f.stage_num);
            nextion_show_error(err);
            return;
        }

        if (!f.t_delta_set) {
            f.t_delta = CONFIG_NEXTION_T_DELTA_MIN_MIN;
            f.t_delta_set = true;
        }

        if (!program_draft_set_stage(f.stage_num, f.t_min, f.target_t, f.t_delta, f.delta_t_x10,
                                     f.t_set, f.target_set, f.t_delta_set, f.delta_t_set)) {
            char err[48];
            snprintf(err, sizeof(err), "Stage %d: Invalid stage", f.stage_num);
            nextion_show_error(err);
            return;
        }
    }

    sync_program_buffer();

    char error_msg[64];
    program_draft_t validated;
    program_draft_get(&validated);
    if (!program_validate_draft_with_temp(&validated, program_get_ambient_temp_c(),
                                         error_msg, sizeof(error_msg))) {
        nextion_show_error(error_msg);
        return;
    }

    char save_error[64];
    if (!nextion_storage_save_program(&validated, s_original_program_name,
                                     save_error, sizeof(save_error))) {
        nextion_show_error(save_error);
    } else {
        nextion_show_error("Saved");
        nextion_send_cmd("errTxtHead.txt=\"Success\"");
        strncpy(s_original_program_name, validated.name,
                sizeof(s_original_program_name) - 1);
        s_original_program_name[sizeof(s_original_program_name) - 1] = '\0';
        nextion_send_cmd("deleteBtn.txt=\"Delete\"");
        nextion_send_cmd("confirmDelete.txt=\"Delete\"");
        nextion_send_cmd("dirty.val=0");
        LOGGER_LOG_INFO(TAG, "Program draft validated and saved to SD");
    }
}

/* ── handle_show_graph ─────────────────────────────────────────────── */

void handle_show_graph(const char *payload)
{
    if (!payload) {
        return;
    }

    if (s_graph_visible) {
        nextion_send_cmd("vis graphDisp,0");
        nextion_send_cmd("showGraph.txt=\"Show Graph\"");
        s_graph_visible = false;
        return;
    }

    char buffer[512];
    char *tokens[PAGE_TOKEN_COUNT] = {0};
    if (!tokenize_and_validate(payload, "Graph", buffer, sizeof(buffer),
                               tokens, PAGE_TOKEN_COUNT)) {
        return;
    }

    program_draft_set_name(trim_in_place(tokens[0]));

    size_t idx = 1;
    int current_temp = starting_temp_for_current_page();

    for (int row = 0; row < PROGRAMS_PAGE_STAGE_COUNT; ++row) {
        StageFields f;
        parse_stage_fields(tokens, &idx, row, &f);

        if (!f.any_set) {
            program_draft_clear_stage(f.stage_num);
            continue;
        }

        if (!f.target_set) {
            continue;
        }

        int temp_diff = f.target_t - current_temp;
        int temp_diff_x10 = temp_diff * 10;

        if (!f.t_set && f.delta_t_set && f.delta_t_x10 != 0) {
            f.t_min = temp_diff_x10 / f.delta_t_x10;
            if (f.t_min < 0) f.t_min = -f.t_min;
            f.t_set = true;
        }

        if (!f.delta_t_set && f.t_set && f.t_min != 0) {
            f.delta_t_x10 = temp_diff_x10 / f.t_min;
            f.delta_t_set = true;
        }

        if (!f.t_delta_set) {
            f.t_delta = CONFIG_NEXTION_T_DELTA_MIN_MIN;
            f.t_delta_set = true;
        }

        program_draft_set_stage(f.stage_num, f.t_min, f.target_t, f.t_delta, f.delta_t_x10,
                                f.t_set, f.target_set, f.t_delta_set, f.delta_t_set);
        current_temp = f.target_t;
    }

    nextion_send_cmd("vis graphDisp,1");
    nextion_send_cmd("showGraph.txt=\"Hide Graph\"");
    s_graph_visible = true;

    program_draft_t graph_draft;
    program_draft_get(&graph_draft);
    render_graph_to_nextion(&graph_draft, CONFIG_NEXTION_PROGRAMS_GRAPH_ID,
                            CONFIG_NEXTION_PROGRAMS_GRAPH_WIDTH,
                            CONFIG_NEXTION_PROGRAMS_GRAPH_HEIGHT);
}

/* ── handle_autofill ───────────────────────────────────────────────── */

void handle_autofill(const char *payload)
{
    if (!payload) {
        return;
    }

    char buffer[512];
    char *tokens[PAGE_TOKEN_COUNT] = {0};
    if (!tokenize_and_validate(payload, "Autofill", buffer, sizeof(buffer),
                               tokens, PAGE_TOKEN_COUNT)) {
        return;
    }

    int current_temp = starting_temp_for_current_page();

    bool any_calculated = false;
    bool any_error = false;
    char error_msg[64] = {0};

    size_t idx = 1;
    char cmd[64];

    for (int row = 0; row < PROGRAMS_PAGE_STAGE_COUNT; ++row) {
        StageFields f;
        parse_stage_fields(tokens, &idx, row, &f);

        if (!f.t_set && !f.target_set && !f.delta_t_set) {
            continue;
        }

        if (!f.target_set) {
            if (f.t_set || f.delta_t_set) {
                snprintf(error_msg, sizeof(error_msg), "Stage %d: Target temp required", f.stage_num);
                any_error = true;
            }
            continue;
        }

        if (!validate_temp_in_range(f.target_t, f.stage_num, error_msg, sizeof(error_msg))) {
            any_error = true;
            continue;
        }

        int temp_diff = f.target_t - current_temp;
        int temp_diff_x10 = temp_diff * 10;

        if (temp_diff == 0) {
            if (!f.delta_t_set) {
                snprintf(cmd, sizeof(cmd), "tempDelta%u.txt=\"0.0\"", (unsigned)f.field_num);
                nextion_send_cmd(cmd);
                any_calculated = true;
            }
            if (!f.t_delta_set) {
                snprintf(cmd, sizeof(cmd), "tDelta%u.txt=\"%d\"", (unsigned)f.field_num, CONFIG_NEXTION_T_DELTA_MIN_MIN);
                nextion_send_cmd(cmd);
                any_calculated = true;
            }
            current_temp = f.target_t;
            continue;
        }

        // Case 1: Have target + delta_T, calculate time
        if (!f.t_set && f.delta_t_set) {
            if (f.delta_t_x10 == 0) {
                snprintf(error_msg, sizeof(error_msg), "Stage %d: Delta T cannot be 0", f.stage_num);
                any_error = true;
                continue;
            }

            if (!validate_delta_t_in_range(f.delta_t_x10, f.stage_num, error_msg, sizeof(error_msg))) {
                any_error = true;
                continue;
            }

            int calc_time;
            if (!autofill_calc_time(temp_diff_x10, f.delta_t_x10,
                    f.stage_num, f.target_t, &calc_time,
                    error_msg, sizeof(error_msg))) {
                any_error = true;
                continue;
            }

            if (!validate_time_in_range(calc_time, f.stage_num, error_msg, sizeof(error_msg))) {
                any_error = true;
                continue;
            }

            snprintf(cmd, sizeof(cmd), "t%u.txt=\"%d\"", (unsigned)f.field_num, calc_time);
            nextion_send_cmd(cmd);
            if (!f.t_delta_set) {
                snprintf(cmd, sizeof(cmd), "tDelta%u.txt=\"%d\"", (unsigned)f.field_num, CONFIG_NEXTION_T_DELTA_MIN_MIN);
                nextion_send_cmd(cmd);
            }
            any_calculated = true;
            current_temp = f.target_t;
            continue;
        }

        // Case 2: Have target + time, calculate delta_T
        if (f.t_set && !f.delta_t_set) {
            if (!validate_time_in_range(f.t_min, f.stage_num, error_msg, sizeof(error_msg))) {
                any_error = true;
                continue;
            }

            int calc_delta_x10;
            if (!autofill_calc_delta(temp_diff_x10, f.t_min,
                    f.stage_num, &calc_delta_x10,
                    error_msg, sizeof(error_msg))) {
                any_error = true;
                continue;
            }

            if (!validate_delta_t_in_range(calc_delta_x10, f.stage_num, error_msg, sizeof(error_msg))) {
                any_error = true;
                continue;
            }

            char delta_buf[16];
            format_delta_x10(calc_delta_x10, delta_buf, sizeof(delta_buf));
            snprintf(cmd, sizeof(cmd), "tempDelta%u.txt=\"%s\"", (unsigned)f.field_num, delta_buf);
            nextion_send_cmd(cmd);
            if (!f.t_delta_set) {
                snprintf(cmd, sizeof(cmd), "tDelta%u.txt=\"%d\"", (unsigned)f.field_num, CONFIG_NEXTION_T_DELTA_MIN_MIN);
                nextion_send_cmd(cmd);
            }
            any_calculated = true;
            current_temp = f.target_t;
            continue;
        }

        // Case 3: Both set - nothing to calculate
        if (f.t_set && f.delta_t_set) {
            if (!f.t_delta_set) {
                snprintf(cmd, sizeof(cmd), "tDelta%u.txt=\"%d\"", (unsigned)f.field_num, CONFIG_NEXTION_T_DELTA_MIN_MIN);
                nextion_send_cmd(cmd);
                any_calculated = true;
            }
            current_temp = f.target_t;
            continue;
        }

        // Case 4: Neither set
        if (!f.t_set && !f.delta_t_set) {
            snprintf(error_msg, sizeof(error_msg), "Stage %d: Need Time or Delta T", f.stage_num);
            any_error = true;
        }
    }

    if (any_error) {
        nextion_show_error(error_msg);
    } else if (any_calculated) {
        nextion_clear_error();
    } else {
        nextion_show_error("Nothing to calculate");
    }
}

/* ── handle_prog_page_data ─────────────────────────────────────────── */

void handle_prog_page_data(const char *payload)
{
    if (!payload) {
        return;
    }

    char buffer[512];
    strncpy(buffer, payload, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *dir = buffer;
    char *comma = strchr(buffer, ',');
    if (!comma) {
        return;
    }
    *comma = '\0';
    char *rest = comma + 1;

    bool go_prev = (strcmp(dir, "prev") == 0);
    bool go_next = (strcmp(dir, "next") == 0);
    if (!go_prev && !go_next) {
        return;
    }

    char *tokens[PAGE_TOKEN_COUNT] = {0};
    size_t token_count = 0;
    char *cursor = rest;
    while (cursor && token_count < PAGE_TOKEN_COUNT) {
        char *c = strchr(cursor, ',');
        if (c) {
            *c = '\0';
        }
        tokens[token_count++] = cursor;
        cursor = c ? c + 1 : NULL;
    }

    if (token_count < PAGE_TOKEN_COUNT) {
        if (go_prev) {
            programs_page_apply((uint8_t)(s_programs_page - 1));
        } else {
            programs_page_apply((uint8_t)(s_programs_page + 1));
        }
        return;
    }

    program_draft_set_name(trim_in_place(tokens[0]));

    size_t idx = 1;
    for (int row = 0; row < PROGRAMS_PAGE_STAGE_COUNT; ++row) {
        StageFields f;
        parse_stage_fields(tokens, &idx, row, &f);

        if (!f.any_set) {
            program_draft_clear_stage(f.stage_num);
            continue;
        }

        if (!f.t_delta_set) {
            f.t_delta = CONFIG_NEXTION_T_DELTA_MIN_MIN;
        }

        program_draft_set_stage(f.stage_num, f.t_min, f.target_t, f.t_delta, f.delta_t_x10,
                                f.t_set, f.target_set, f.t_delta_set, f.delta_t_set);
    }

    if (go_prev) {
        programs_page_apply((uint8_t)(s_programs_page - 1));
    } else {
        programs_page_apply((uint8_t)(s_programs_page + 1));
    }

    sync_program_buffer();
}

/* ── Confirm dialog helper ──────────────────────────────────────────── */

static void show_confirm_dialog(dialog_context_t context,
                                const char *message,
                                const char *action_text)
{
    s_dialog_context = context;

    char cmd[96];
    snprintf(cmd, sizeof(cmd), "confirmTxt.txt=\"%.40s\"", message);
    nextion_send_cmd(cmd);
    vTaskDelay(pdMS_TO_TICKS(20));
    snprintf(cmd, sizeof(cmd), "confirmDelete.txt=\"%s\"", action_text);
    nextion_send_cmd(cmd);
    vTaskDelay(pdMS_TO_TICKS(20));
    nextion_send_cmd("vis confirmBdy,1");
    vTaskDelay(pdMS_TO_TICKS(20));
    nextion_send_cmd("vis confirmTxt,1");
    vTaskDelay(pdMS_TO_TICKS(20));
    nextion_send_cmd("vis confirmDelete,1");
    vTaskDelay(pdMS_TO_TICKS(20));
    nextion_send_cmd("vis confirmCancel,1");
}

static void hide_confirm_dialog(void)
{
    nextion_send_cmd("vis confirmBdy,0");
    nextion_send_cmd("vis confirmTxt,0");
    nextion_send_cmd("vis confirmDelete,0");
    nextion_send_cmd("vis confirmCancel,0");
    s_dialog_context = DIALOG_NONE;
}

/* ── handle_add_prog ───────────────────────────────────────────────── */

void handle_add_prog(void)
{
    s_original_program_name[0] = '\0';
    program_draft_clear();
    s_programs_page = 1;
    s_graph_visible = false;
    s_dialog_context = DIALOG_NONE;
    nextion_send_cmd("page " CONFIG_NEXTION_PAGE_PROGRAMS);
    vTaskDelay(pdMS_TO_TICKS(30));
    programs_page_apply(1);
    nextion_send_cmd("progNameInput.txt=\"\"");
    nextion_send_cmd("deleteBtn.txt=\"Clear\"");
    nextion_send_cmd("confirmDelete.txt=\"Clear\"");
    nextion_send_cmd("dirty.val=0");
    sync_program_buffer();
}

/* ── handle_delete_prog / handle_confirm_delete ────────────────────── */

void handle_delete_prog(const char *current_name)
{
    if (s_original_program_name[0] == '\0') {
        /* New program mode — "Clear" action */
        show_confirm_dialog(DIALOG_CLEAR,
                            "Clear all program data?",
                            "Clear");
        return;
    }

    const char *name = current_name ? trim_in_place((char *)current_name) : "";
    if (strcmp(name, s_original_program_name) != 0) {
        nextion_show_error("Restore original name to delete");
        return;
    }

    char msg[48];
    snprintf(msg, sizeof(msg), "Delete \"%.26s\"?", s_original_program_name);
    show_confirm_dialog(DIALOG_DELETE, msg, "Delete");
}

void handle_confirm_delete(void)
{
    dialog_context_t ctx = s_dialog_context;
    hide_confirm_dialog();

    switch (ctx) {
        case DIALOG_CLEAR:
            /* New program mode — clear all fields */
            program_draft_clear();
            s_programs_page = 1;
            programs_page_apply(1);
            nextion_send_cmd("progNameInput.txt=\"\"");
            nextion_send_cmd("dirty.val=0");
            sync_program_buffer();
            break;

        case DIALOG_EXIT:
            /* Dirty exit — navigate to main without saving */
            nextion_set_current_page(NEXTION_PAGE_ID_MAIN);
            nextion_send_cmd("page " CONFIG_NEXTION_PAGE_MAIN);
            break;

        case DIALOG_DELETE:
        default: {
            /* Original behavior — delete from SD */
            char error_msg[64];
            if (!nextion_storage_delete_program(s_original_program_name,
                                                error_msg, sizeof(error_msg))) {
                nextion_show_error(error_msg);
                s_dialog_context = DIALOG_NONE;
                return;
            }

            s_original_program_name[0] = '\0';
            program_draft_clear();
            s_programs_page = 1;
            programs_page_apply(1);
            nextion_send_cmd("progNameInput.txt=\"\"");
            sync_program_buffer();
            break;
        }
    }

    s_dialog_context = DIALOG_NONE;
}

/* ── handle_prog_back ──────────────────────────────────────────────── */

void handle_prog_back(const char *payload)
{
    (void)payload;
    show_confirm_dialog(DIALOG_EXIT,
                        "Unsaved changes will be lost. Exit?",
                        "Exit");
}

/* ── handle_edit_prog ──────────────────────────────────────────────── */

void handle_edit_prog(const char *payload)
{
    if (!payload) {
        return;
    }

    char name[64];
    strncpy(name, payload, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    char *trimmed = trim_in_place(name);
    if (trimmed != name) {
        memmove(name, trimmed, strlen(trimmed) + 1);
    }

    if (name[0] == '\0') {
        nextion_show_error("No program selected");
        return;
    }

    char path[128];
    if (strstr(name, ".")) {
        snprintf(path, sizeof(path), "sd0/%s", name);
    } else {
        snprintf(path, sizeof(path), "sd0/%s%s", name, CONFIG_NEXTION_PROGRAM_FILE_EXTENSION);
    }

    char error_msg[64];
    if (!nextion_storage_parse_file_to_draft(name, error_msg, sizeof(error_msg))) {
        nextion_show_error(error_msg);
        return;
    }

    strncpy(s_original_program_name, program_draft_get_name(),
            sizeof(s_original_program_name) - 1);
    s_original_program_name[sizeof(s_original_program_name) - 1] = '\0';
    s_programs_page = 1;
    s_graph_visible = false;
    s_dialog_context = DIALOG_NONE;

    nextion_send_cmd("page " CONFIG_NEXTION_PAGE_PROGRAMS);
    vTaskDelay(pdMS_TO_TICKS(30));
    programs_page_apply(1);

    char cmd[96];
    snprintf(cmd, sizeof(cmd), "progNameInput.txt=\"%s\"", program_draft_get_name());
    nextion_send_cmd(cmd);
    nextion_send_cmd("deleteBtn.txt=\"Delete\"");
    nextion_send_cmd("confirmDelete.txt=\"Delete\"");
    nextion_send_cmd("dirty.val=0");

    sync_program_buffer();
}

/* ── handle_program_select ─────────────────────────────────────────── */

void handle_program_select(const char *filename)
{
    if (!filename || filename[0] == '\0') {
        return;
    }

    while (*filename == ' ' || *filename == '\t') {
        ++filename;
    }
    if (*filename == '\0') {
        return;
    }
    size_t name_len = strlen(filename);
    if (name_len == 0 || name_len > 63) {
        nextion_show_error("Invalid filename");
        return;
    }

    LOGGER_LOG_INFO(TAG, "Program load: %s", filename);

    char error_msg[64];
    if (!nextion_storage_parse_file_to_draft(filename, error_msg, sizeof(error_msg))) {
        nextion_show_error(error_msg);
        return;
    }

    program_draft_t parsed;
    program_draft_get(&parsed);
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "progNameDisp.txt=\"%s\"", parsed.name);
    nextion_send_cmd(cmd);

    int total_time = 0;
    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        const program_stage_t *stage = &parsed.stages[i];
        if (!stage->is_set) {
            continue;
        }
        total_time += stage->t_min;
    }

    LOGGER_LOG_INFO(TAG, "Program parsed: name=%s time=%d", parsed.name, total_time);

    snprintf(cmd, sizeof(cmd), "timeElapsed.txt=\"0\"");
    nextion_send_cmd(cmd);
    snprintf(cmd, sizeof(cmd), "timeRamaining.txt=\"%d\"", total_time);
    nextion_send_cmd(cmd);

    render_graph_to_nextion(&parsed, CONFIG_NEXTION_GRAPH_DISP_ID,
                            CONFIG_NEXTION_MAIN_GRAPH_WIDTH,
                            CONFIG_NEXTION_MAIN_GRAPH_HEIGHT);
    sync_program_buffer();
}

/* ── handle_prog_field_set (keyboard return validation) ─────────────── */

void handle_prog_field_set(const char *payload)
{
    if (!payload) return;

    char buffer[64];
    strncpy(buffer, payload, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *comma = strchr(buffer, ',');
    if (!comma) return;
    *comma = '\0';
    const char *value = comma + 1;

    int code;
    if (!parse_int(buffer, &code) || code < 11) return;

    int field_type = code / 10;   /* 1=time, 2=target, 3=tDelta, 4=tempDelta */
    int field_row  = code % 10;   /* 1-5 */
    if (field_row < 1 || field_row > PROGRAMS_PAGE_STAGE_COUNT) return;

    int stage_num = (s_programs_page - 1) * PROGRAMS_PAGE_STAGE_COUNT + field_row;

    /* Empty value = field cleared — always valid */
    const char *trimmed = value;
    while (*trimmed == ' ') trimmed++;
    if (*trimmed == '\0') return;

    char err[64];

    switch (field_type) {
    case 1: { /* time (minutes) */
        int t_min;
        if (!parse_int(trimmed, &t_min)) {
            nextion_show_error("Invalid time value");
            send_int_field("t", (uint8_t)field_row, 0, false);
            return;
        }
        if (!validate_time_in_range(t_min, stage_num, err, sizeof(err))) {
            nextion_show_error(err);
        }
        break;
    }
    case 2: { /* target temperature */
        int target_t;
        if (!parse_int(trimmed, &target_t)) {
            nextion_show_error("Invalid temperature value");
            send_int_field("targetTMax", (uint8_t)field_row, 0, false);
            return;
        }
        if (!validate_temp_in_range(target_t, stage_num, err, sizeof(err))) {
            nextion_show_error(err);
        }
        break;
    }
    case 3: { /* temp delta (°C/min, decimal x10) */
        int delta_x10;
        if (!parse_decimal_x10(trimmed, &delta_x10)) {
            nextion_show_error("Invalid delta T value");
            send_delta_field((uint8_t)field_row, 0, false);
            return;
        }
        if (!validate_delta_t_in_range(delta_x10, stage_num, err, sizeof(err))) {
            nextion_show_error(err);
        }
        break;
    }
    case 4: { /* time delta (minutes) */
        int t_delta;
        if (!parse_int(trimmed, &t_delta)) {
            nextion_show_error("Invalid time delta value");
            send_int_field("tDelta", (uint8_t)field_row, 0, false);
            return;
        }
        if (t_delta < CONFIG_NEXTION_T_DELTA_MIN_MIN) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Stage %d: Time delta min is %d",
                     stage_num, CONFIG_NEXTION_T_DELTA_MIN_MIN);
            nextion_show_error(msg);
        }
        break;
    }
    default:
        break;
    }
}

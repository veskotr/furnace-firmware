#include "nextion_events_internal.h"

#include "sdkconfig.h"
#include "nextion_file_reader_internal.h"
#include "nextion_transport_internal.h"
#include "nextion_storage_internal.h"
#include "nextion_ui_internal.h"
#include "heating_program_models.h"
#include "heating_program_models_internal.h"
#include "heating_program_graph_internal.h"
#include "heating_program_validation.h"
#include "event_manager.h"
#include "event_registry.h"
#include "logger_component.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "nextion_events";
static uint8_t s_programs_page = 1;
static bool s_graph_visible = false;
static char s_original_program_name[64] = {0};

// Live waveform tracking (Phase 4: two-channel graph)
static bool     s_waveform_active = false;   // true while a profile is running
static uint32_t s_waveform_total_ms = 0;     // total duration of the loaded profile
static uint32_t s_waveform_x = 0;            // current pixel position on channel 1

// Forward declarations
static void handle_settings_init(void);
static void format_delta_x10(int val_x10, char *buf, size_t buf_len);
static void update_main_status(void);
static void sync_program_buffer(void);
static bool load_program_into_draft(const char *filename, char *error_msg, size_t error_len);
static void programs_page_apply(uint8_t page);

static void handle_run_start(void)
{
    char error_msg[96] = {0};
    ProgramDraft snapshot = *program_draft_get();

    if (!program_validate_draft_with_temp(&snapshot, program_get_current_temp_c(),
                                         error_msg, sizeof(error_msg))) {
        nextion_show_error(error_msg);
        return;
    }

    // Copy validated draft into the run slot so the coordinator reads a stable snapshot
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

static void handle_run_pause(void)
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

static void handle_run_stop(void)
{
    esp_err_t err = event_manager_post_blocking(
        COORDINATOR_EVENT,
        COORDINATOR_EVENT_STOP_PROFILE,
        NULL,
        0);

    if (err != ESP_OK) {
        nextion_show_error("Stop failed");
    }
}

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

static bool serialize_program_to_buffer(const ProgramDraft *draft, char *out, size_t out_len)
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

        written = snprintf(out + used, out_len - used,
                           "stage=%d,t=%d,target=%d,tdelta=%d,delta_x10=%d\n",
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

    return used < out_len;
}

static void sync_program_buffer(void)
{
    char *payload = malloc(CONFIG_NEXTION_PROGRAM_FILE_SIZE);
    if (!payload) {
        return;
    }
    memset(payload, 0, CONFIG_NEXTION_PROGRAM_FILE_SIZE);
    if (!serialize_program_to_buffer(program_draft_get(), payload, CONFIG_NEXTION_PROGRAM_FILE_SIZE)) {
        free(payload);
        return;
    }
    nextion_set_text_chunked("programBuffer", payload);
    free(payload);
}

static bool load_program_into_draft(const char *filename, char *error_msg, size_t error_len)
{
    return nextion_storage_parse_file_to_draft(filename, error_msg, error_len);
}

static void programs_page_apply(uint8_t page)
{
    if (page < 1) {
        page = 1;
    } else if (page > PROGRAMS_PAGE_COUNT) {
        page = PROGRAMS_PAGE_COUNT;
    }

    s_programs_page = page;

    char cmd[64];

    // Update page number display
    snprintf(cmd, sizeof(cmd), "pageNum.txt=\"%u\"", (unsigned)page);
    nextion_send_cmd(cmd);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Update stage labels (bStg1-5) to show correct stage numbers for this page
    for (uint8_t i = 0; i < PROGRAMS_PAGE_STAGE_COUNT; ++i) {
        uint8_t stage_num = (uint8_t)((page - 1) * PROGRAMS_PAGE_STAGE_COUNT + (i + 1));
        snprintf(cmd, sizeof(cmd), "bStg%u.txt=\"%u\"", (unsigned)(i + 1), (unsigned)stage_num);
        nextion_send_cmd(cmd);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Load draft data for this page into the UI fields
    const ProgramDraft *draft = program_draft_get();
    for (uint8_t i = 0; i < PROGRAMS_PAGE_STAGE_COUNT; ++i) {
        uint8_t stage_idx = (uint8_t)((page - 1) * PROGRAMS_PAGE_STAGE_COUNT + i);
        const ProgramStage *stage = &draft->stages[stage_idx];
        uint8_t field_num = i + 1;

        if (stage->is_set) {
            if (stage->t_set) {
                snprintf(cmd, sizeof(cmd), "t%u.txt=\"%d\"", (unsigned)field_num, stage->t_min);
            } else {
                snprintf(cmd, sizeof(cmd), "t%u.txt=\"\"", (unsigned)field_num);
            }
            nextion_send_cmd(cmd);

            if (stage->target_set) {
                snprintf(cmd, sizeof(cmd), "targetTMax%u.txt=\"%d\"", (unsigned)field_num, stage->target_t_c);
            } else {
                snprintf(cmd, sizeof(cmd), "targetTMax%u.txt=\"\"", (unsigned)field_num);
            }
            nextion_send_cmd(cmd);

            if (stage->t_delta_set) {
                snprintf(cmd, sizeof(cmd), "tDelta%u.txt=\"%d\"", (unsigned)field_num, stage->t_delta_min);
            } else {
                snprintf(cmd, sizeof(cmd), "tDelta%u.txt=\"\"", (unsigned)field_num);
            }
            nextion_send_cmd(cmd);

            if (stage->delta_t_set) {
                char delta_buf[16];
                format_delta_x10(stage->delta_t_per_min_x10, delta_buf, sizeof(delta_buf));
                snprintf(cmd, sizeof(cmd), "tempDelta%u.txt=\"%s\"", (unsigned)field_num, delta_buf);
            } else {
                snprintf(cmd, sizeof(cmd), "tempDelta%u.txt=\"\"", (unsigned)field_num);
            }
            nextion_send_cmd(cmd);
        } else {
            snprintf(cmd, sizeof(cmd), "t%u.txt=\"\"", (unsigned)field_num);
            nextion_send_cmd(cmd);
            snprintf(cmd, sizeof(cmd), "targetTMax%u.txt=\"\"", (unsigned)field_num);
            nextion_send_cmd(cmd);
            snprintf(cmd, sizeof(cmd), "tDelta%u.txt=\"\"", (unsigned)field_num);
            nextion_send_cmd(cmd);
            snprintf(cmd, sizeof(cmd), "tempDelta%u.txt=\"\"", (unsigned)field_num);
            nextion_send_cmd(cmd);
        }
    }
}

static bool parse_int(const char *text, int *out_value)
{
    if (!text || !out_value) {
        return false;
    }

    if (*text == '\0') {
        return false;
    }

    char *endptr = NULL;
    long value = strtol(text, &endptr, 10);
    if (*endptr != '\0') {
        return false;
    }

    *out_value = (int)value;
    return true;
}

// Parse a decimal number and return x10 fixed-point integer
// Examples: "1.5" -> 15, "3" -> 30, "-0.5" -> -5, "2.7" -> 27
static bool parse_decimal_x10(const char *text, int *out_value_x10)
{
    if (!text || !out_value_x10) {
        return false;
    }

    if (*text == '\0') {
        return false;
    }

    // Handle sign
    int sign = 1;
    const char *ptr = text;
    if (*ptr == '-') {
        sign = -1;
        ptr++;
    } else if (*ptr == '+') {
        ptr++;
    }

    // Parse whole part
    int whole = 0;
    while (*ptr >= '0' && *ptr <= '9') {
        whole = whole * 10 + (*ptr - '0');
        ptr++;
    }

    // Parse fractional part
    int frac = 0;
    if (*ptr == '.') {
        ptr++;
        if (*ptr >= '0' && *ptr <= '9') {
            frac = *ptr - '0';  // Take only first decimal digit
            ptr++;
            // Skip any remaining decimal digits
            while (*ptr >= '0' && *ptr <= '9') {
                ptr++;
            }
        }
    }

    // Check we consumed all input
    if (*ptr != '\0') {
        return false;
    }

    *out_value_x10 = sign * (whole * 10 + frac);
    return true;
}

// Format x10 value — delegates to shared helper from program_validation
static void format_delta_x10(int val_x10, char *buf, size_t buf_len)
{
    format_x10_value(val_x10, buf, buf_len);
}

static char *trim_in_place(char *text)
{
    if (!text) {
        return text;
    }

    while (*text == ' ' || *text == '\t') {
        ++text;
    }

    char *end = text + strlen(text);
    while (end > text) {
        char c = *(end - 1);
        if (c != ' ' && c != '\t') {
            break;
        }
        --end;
    }
    *end = '\0';
    return text;
}

static bool parse_optional_int(const char *text, int *out_value, bool *is_set)
{
    if (!is_set) {
        return false;
    }
    if (!text || text[0] == '\0') {
        *is_set = false;
        return true;
    }

    char *trimmed = trim_in_place((char *)text);
    if (trimmed[0] == '\0') {
        *is_set = false;
        return true;
    }

    int value = 0;
    if (!parse_int(trimmed, &value)) {
        return false;
    }

    *out_value = value;
    *is_set = true;
    return true;
}

// Parse optional delta_T that may arrive as decimal text or x10-prefixed integer
// Accepted formats:
//  - "1.5" -> 15
//  - "x10=15" -> 15
static bool parse_optional_delta_x10(const char *text, int *out_value_x10, bool *is_set)
{
    if (!is_set) {
        return false;
    }
    if (!text || text[0] == '\0') {
        *is_set = false;
        return true;
    }

    char *trimmed = trim_in_place((char *)text);
    if (trimmed[0] == '\0') {
        *is_set = false;
        return true;
    }

    int value_x10 = 0;
    if (strncmp(trimmed, "x10=", 4) == 0) {
        if (!parse_int(trimmed + 4, &value_x10)) {
            return false;
        }
    } else {
        if (!parse_decimal_x10(trimmed, &value_x10)) {
            return false;
        }
    }

    *out_value_x10 = value_x10;
    *is_set = true;
    return true;
}

static void handle_save_prog(const char *payload)
{
    if (!payload) {
        return;
    }

    // Method 2: Payload contains name + current page's 5 stages (5 fields each: bStg,t,target,tdelta,tempdelta)
    // Format: name,bStg1,t1,target1,tdelta1,tempdelta1,...,bStg5,t5,target5,tdelta5,tempdelta5
    // Total: 1 name + 5 stages * 5 fields = 26 tokens
    #define SAVE_TOKEN_COUNT (1 + PROGRAMS_PAGE_STAGE_COUNT * 5)
    
    char buffer[512];
    strncpy(buffer, payload, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *tokens[SAVE_TOKEN_COUNT] = {0};
    size_t token_count = 0;

    char *cursor = buffer;
    while (cursor && token_count < SAVE_TOKEN_COUNT) {
        char *comma = strchr(cursor, ',');
        if (comma) {
            *comma = '\0';
        }
        tokens[token_count++] = cursor;
        cursor = comma ? comma + 1 : NULL;
    }

    if (token_count < SAVE_TOKEN_COUNT) {
        char err[64];
        snprintf(err, sizeof(err), "Missing fields: got %u, need %d", (unsigned)token_count, SAVE_TOKEN_COUNT);
        nextion_show_error(err);
        return;
    }

    // Set program name
    program_draft_set_name(trim_in_place(tokens[0]));

    // Parse current page's 5 stages into draft
    // SAVE does NOT auto-calculate. User must either:
    // 1. Provide all 3 fields (time, target_temp, delta_t) - we validate consistency
    // 2. Use Autofill button first to calculate missing time or delta_t
    size_t idx = 1;

    for (int row = 0; row < PROGRAMS_PAGE_STAGE_COUNT; ++row) {
        uint8_t stage_num = (uint8_t)((s_programs_page - 1) * PROGRAMS_PAGE_STAGE_COUNT + row + 1);
        
        int t_min = 0;
        int target_t = 0;
        int t_delta = 0;
        int delta_t_x10 = 0;  // x10 fixed-point: 15 = 1.5°C/min
        bool t_set = false;
        bool target_set = false;
        bool t_delta_set = false;
        bool delta_t_set = false;

        idx++; // skip bStg label

        if (!parse_optional_int(tokens[idx++], &t_min, &t_set) ||
            !parse_optional_int(tokens[idx++], &target_t, &target_set) ||
            !parse_optional_int(tokens[idx++], &t_delta, &t_delta_set) ||
            !parse_optional_delta_x10(tokens[idx++], &delta_t_x10, &delta_t_set)) {
            nextion_show_error("Invalid numeric input");
            return;
        }

        bool any_set = t_set || target_set || t_delta_set || delta_t_set;
        if (!any_set) {
            // Clear this stage in draft
            program_draft_clear_stage(stage_num);
            continue;
        }

        // Target temp is always required
        if (!target_set) {
            char err[48];
            snprintf(err, sizeof(err), "Stage %d: Target temp required", stage_num);
            nextion_show_error(err);
            return;
        }

        // Require BOTH time AND delta_t for save (no auto-calculation)
        // User must use Autofill button to calculate missing values
        if (!t_set && !delta_t_set) {
            char err[64];
            snprintf(err, sizeof(err), "Stage %d: Add Time & Delta T or use Autofill", stage_num);
            nextion_show_error(err);
            return;
        }

        if (!t_set) {
            char err[64];
            snprintf(err, sizeof(err), "Stage %d: Time missing. Use Autofill", stage_num);
            nextion_show_error(err);
            return;
        }

        if (!delta_t_set) {
            char err[64];
            snprintf(err, sizeof(err), "Stage %d: Delta T missing. Use Autofill", stage_num);
            nextion_show_error(err);
            return;
        }

        // Default t_delta if not set
        if (!t_delta_set) {
            t_delta = CONFIG_NEXTION_T_DELTA_MIN_MIN;
            t_delta_set = true;
        }

        if (!program_draft_set_stage(stage_num,
                                     t_min,
                                     target_t,
                                     t_delta,
                                     delta_t_x10,
                                     t_set,
                                     target_set,
                                     t_delta_set,
                                     delta_t_set)) {
            char err[48];
            snprintf(err, sizeof(err), "Stage %d: Invalid stage", stage_num);
            nextion_show_error(err);
            return;
        }
    }

    sync_program_buffer();

    // Validate the full draft (includes mathematical consistency checks)
    char error_msg[64];

    if (!program_validate_draft_with_temp(program_draft_get(), program_get_current_temp_c(),
                                         error_msg, sizeof(error_msg))) {
        nextion_show_error(error_msg);
        return;
    }

    // Save to SD (runs inline on coordinator task — no concurrent access)
    char save_error[64];
    if (!nextion_storage_save_program(program_draft_get(), s_original_program_name, save_error, sizeof(save_error))) {
        nextion_show_error(save_error);
    } else {
        nextion_clear_error();
        strncpy(s_original_program_name, program_draft_get()->name, sizeof(s_original_program_name) - 1);
        s_original_program_name[sizeof(s_original_program_name) - 1] = '\0';
        LOGGER_LOG_INFO(TAG, "Program draft validated and saved to SD");
    }
}

static void handle_show_graph(const char *payload)
{
    if (!payload) {
        return;
    }

    // Toggle: if graph is visible, hide it and return
    if (s_graph_visible) {
        nextion_send_cmd("vis graphDisp,0");
        s_graph_visible = false;
        return;
    }

    // Method 2: Payload contains name + current page's 5 stages (5 fields each)
    // Format: name,bStg1,t1,target1,tdelta1,tempdelta1,...
    #define GRAPH_TOKEN_COUNT (1 + PROGRAMS_PAGE_STAGE_COUNT * 5)
    
    char buffer[512];
    strncpy(buffer, payload, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *tokens[GRAPH_TOKEN_COUNT] = {0};
    size_t token_count = 0;

    char *cursor = buffer;
    while (cursor && token_count < GRAPH_TOKEN_COUNT) {
        char *comma = strchr(cursor, ',');
        if (comma) {
            *comma = '\0';
        }
        tokens[token_count++] = cursor;
        cursor = comma ? comma + 1 : NULL;
    }

    if (token_count < GRAPH_TOKEN_COUNT) {
        char err[64];
        snprintf(err, sizeof(err), "Graph: got %u fields, need %d", (unsigned)token_count, GRAPH_TOKEN_COUNT);
        nextion_show_error(err);
        return;
    }

    // Update draft name
    program_draft_set_name(trim_in_place(tokens[0]));

    // Parse current page's 5 stages into draft (like save does)
    size_t idx = 1;
    int current_temp = program_get_current_temp_c();
    
    // Calculate starting temperature from previous stages in draft
    const ProgramDraft *draft = program_draft_get();
    for (int i = 0; i < (s_programs_page - 1) * PROGRAMS_PAGE_STAGE_COUNT; ++i) {
        if (draft->stages[i].is_set && draft->stages[i].target_set) {
            current_temp = draft->stages[i].target_t_c;
        }
    }

    for (int row = 0; row < PROGRAMS_PAGE_STAGE_COUNT; ++row) {
        uint8_t stage_num = (uint8_t)((s_programs_page - 1) * PROGRAMS_PAGE_STAGE_COUNT + row + 1);
        
        int t_min = 0;
        int target_t = 0;
        int t_delta = 0;
        int delta_t_x10 = 0;
        bool t_set = false;
        bool target_set = false;
        bool t_delta_set = false;
        bool delta_t_set = false;

        idx++; // skip bStg label

        parse_optional_int(tokens[idx++], &t_min, &t_set);
        parse_optional_int(tokens[idx++], &target_t, &target_set);
        parse_optional_int(tokens[idx++], &t_delta, &t_delta_set);
        parse_optional_delta_x10(tokens[idx++], &delta_t_x10, &delta_t_set);

        bool any_set = t_set || target_set || t_delta_set || delta_t_set;
        if (!any_set) {
            program_draft_clear_stage(stage_num);
            continue;
        }

        if (!target_set) {
            continue;
        }

        int temp_diff = target_t - current_temp;
        int temp_diff_x10 = temp_diff * 10;

        if (!t_set && delta_t_set && delta_t_x10 != 0) {
            t_min = temp_diff_x10 / delta_t_x10;
            if (t_min < 0) t_min = -t_min;
            t_set = true;
        }

        if (!delta_t_set && t_set && t_min != 0) {
            delta_t_x10 = temp_diff_x10 / t_min;
            delta_t_set = true;
        }

        if (!t_delta_set) {
            t_delta = CONFIG_NEXTION_T_DELTA_MIN_MIN;
            t_delta_set = true;
        }

        program_draft_set_stage(stage_num,
                                t_min,
                                target_t,
                                t_delta,
                                delta_t_x10,
                                t_set,
                                target_set,
                                t_delta_set,
                                delta_t_set);
        current_temp = target_t;
    }

    // Show the graph component and set visible flag
    nextion_send_cmd("vis graphDisp,1");
    s_graph_visible = true;

    // Clear and render using full draft
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "cle %d,0", CONFIG_NEXTION_PROGRAMS_GRAPH_ID);
    nextion_send_cmd(cmd);

    uint8_t *samples = malloc(CONFIG_NEXTION_PROGRAMS_GRAPH_WIDTH);
    if (!samples) {
        nextion_show_error("Graph: out of memory");
        return;
    }

    size_t count = program_build_graph(program_draft_get(), samples, CONFIG_NEXTION_PROGRAMS_GRAPH_WIDTH, CONFIG_NEXTION_PROGRAMS_GRAPH_HEIGHT, CONFIG_NEXTION_MAX_TEMPERATURE_C, program_get_current_temp_c());
    if (count == 0) {
        nextion_show_error("Graph: no data to render");
        free(samples);
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        snprintf(cmd, sizeof(cmd), "add %d,0,%u", CONFIG_NEXTION_PROGRAMS_GRAPH_ID, (unsigned)samples[i]);
        nextion_send_cmd(cmd);
        if ((i % 64) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    free(samples);
}

static void handle_autofill(const char *payload)
{
    if (!payload) {
        return;
    }

    // Parse payload: name + 5 stages (5 fields each: bStg,t,target,tdelta,tempdelta)
    #define AUTOFILL_TOKEN_COUNT (1 + PROGRAMS_PAGE_STAGE_COUNT * 5)
    
    char buffer[512];
    strncpy(buffer, payload, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *tokens[AUTOFILL_TOKEN_COUNT] = {0};
    size_t token_count = 0;

    char *cursor = buffer;
    while (cursor && token_count < AUTOFILL_TOKEN_COUNT) {
        char *comma = strchr(cursor, ',');
        if (comma) {
            *comma = '\0';
        }
        tokens[token_count++] = cursor;
        cursor = comma ? comma + 1 : NULL;
    }

    if (token_count < AUTOFILL_TOKEN_COUNT) {
        char err[64];
        snprintf(err, sizeof(err), "Autofill: got %u fields, need %d", (unsigned)token_count, AUTOFILL_TOKEN_COUNT);
        nextion_show_error(err);
        return;
    }

    // Get current temperature as starting point
    int current_temp = program_get_current_temp_c();
    
    // For stages on page 2+, we need to calculate current_temp from draft
    const ProgramDraft *draft = program_draft_get();
    for (int i = 0; i < (s_programs_page - 1) * PROGRAMS_PAGE_STAGE_COUNT; ++i) {
        if (draft->stages[i].is_set && draft->stages[i].target_set) {
            current_temp = draft->stages[i].target_t_c;
        }
    }

    // Track if we made any calculations and any errors
    bool any_calculated = false;
    bool any_error = false;
    char error_msg[64] = {0};

    // Process each of the 5 stages on current page
    size_t idx = 1;  // Skip name token
    char cmd[64];

    for (int row = 0; row < PROGRAMS_PAGE_STAGE_COUNT; ++row) {
        uint8_t field_num = (uint8_t)(row + 1);
        uint8_t stage_num = (uint8_t)((s_programs_page - 1) * PROGRAMS_PAGE_STAGE_COUNT + row + 1);

        // Parse the 5 fields for this stage
        idx++;  // skip bStg label
        
        const char *t_str = tokens[idx++];
        const char *target_str = tokens[idx++];
        const char *tdelta_str = tokens[idx++];
        const char *tempdelta_str = tokens[idx++];

        // Parse values - delta_t is x10 fixed-point for 0.1 precision
        int t_min = 0;
        int target_t = 0;
        int t_delta = 0;
        int delta_t_x10 = 0;
        bool t_set = false;
        bool target_set = false;
        bool t_delta_set = false;
        bool delta_t_set = false;

        parse_optional_int(t_str, &t_min, &t_set);
        parse_optional_int(target_str, &target_t, &target_set);
        parse_optional_int(tdelta_str, &t_delta, &t_delta_set);
        parse_optional_delta_x10(tempdelta_str, &delta_t_x10, &delta_t_set);

        // Skip completely empty stages
        if (!t_set && !target_set && !delta_t_set) {
            continue;
        }

        // Must have target temp to do any calculation
        if (!target_set) {
            if (t_set || delta_t_set) {
                snprintf(error_msg, sizeof(error_msg), "Stage %d: Target temp required", stage_num);
                any_error = true;
            }
            continue;
        }

        // Validate target temp against config
        if (!validate_temp_in_range(target_t, stage_num, error_msg, sizeof(error_msg))) {
            any_error = true;
            continue;
        }

        // Calculate temperature difference
        int temp_diff = target_t - current_temp;
        int temp_diff_x10 = temp_diff * 10;

        // Edge case: T = 0 means maintain temperature
        if (temp_diff == 0) {
            // Fill delta_T = 0 if not set
            if (!delta_t_set) {
                snprintf(cmd, sizeof(cmd), "tempDelta%u.txt=\"0.0\"", (unsigned)field_num);
                nextion_send_cmd(cmd);
                any_calculated = true;
            }
            current_temp = target_t;
            continue;
        }

        // Case 1: Have target + delta_T, calculate time
        // t = temp_diff_x10 / delta_t_x10
        if (!t_set && delta_t_set) {
            if (delta_t_x10 == 0) {
                snprintf(error_msg, sizeof(error_msg), "Stage %d: Delta T cannot be 0", stage_num);
                any_error = true;
                continue;
            }
            
            // Validate delta_t against config limits
            if (!validate_delta_t_in_range(delta_t_x10, stage_num, error_msg, sizeof(error_msg))) {
                any_error = true;
                continue;
            }
            
            // Calculate time: t = temp_diff_x10 / delta_t_x10
            int calc_time = temp_diff_x10 / delta_t_x10;
            if (calc_time < 0) calc_time = -calc_time;
            
            if (calc_time < 1) calc_time = 1;  // Minimum 1 minute
            
            // Validate calculated time against config
            if (!validate_time_in_range(calc_time, stage_num, error_msg, sizeof(error_msg))) {
                any_error = true;
                continue;
            }
            
            snprintf(cmd, sizeof(cmd), "t%u.txt=\"%d\"", (unsigned)field_num, calc_time);
            nextion_send_cmd(cmd);
            any_calculated = true;
            current_temp = target_t;
            continue;
        }

        // Case 2: Have target + time, calculate delta_T
        // delta_t_x10 = temp_diff_x10 / t
        if (t_set && !delta_t_set) {
            // Validate time input
            if (!validate_time_in_range(t_min, stage_num, error_msg, sizeof(error_msg))) {
                any_error = true;
                continue;
            }
            
            // Calculate delta_T in x10 fixed-point
            int calc_delta_x10 = temp_diff_x10 / t_min;
            
            // Validate calculated delta_t against config limits
            if (!validate_delta_t_in_range(calc_delta_x10, stage_num, error_msg, sizeof(error_msg))) {
                any_error = true;
                continue;
            }
            
            // Format as decimal for display
            char delta_buf[16];
            format_delta_x10(calc_delta_x10, delta_buf, sizeof(delta_buf));
            snprintf(cmd, sizeof(cmd), "tempDelta%u.txt=\"%s\"", (unsigned)field_num, delta_buf);
            nextion_send_cmd(cmd);
            any_calculated = true;
            current_temp = target_t;
            continue;
        }

        // Case 3: Both time and delta_T are set - nothing to calculate
        if (t_set && delta_t_set) {
            current_temp = target_t;
            continue;
        }

        // Case 4: Neither time nor delta_T - need one of them
        if (!t_set && !delta_t_set) {
            snprintf(error_msg, sizeof(error_msg), "Stage %d: Need Time or Delta T", stage_num);
            any_error = true;
        }
    }

    // Show result
    if (any_error) {
        nextion_show_error(error_msg);
    } else if (any_calculated) {
        nextion_clear_error();
    } else {
        nextion_show_error("Nothing to calculate");
    }
}

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
        // Clear draft and reset to page 1 when entering programs page
        s_original_program_name[0] = '\0';
        program_draft_clear();
        s_programs_page = 1;
        s_graph_visible = false;
        nextion_send_cmd("page " CONFIG_NEXTION_PAGE_PROGRAMS);
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
        vTaskDelay(pdMS_TO_TICKS(50));  // Wait for page to load
        handle_settings_init();
        return;
    }

    LOGGER_LOG_WARN(TAG, "Unknown nav destination: %s", destination);
}

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

// Handle settings page init: send config and user config values to HMI
static void handle_settings_init(void)
{
    char cmd[64];

    // Send hardware/safety limits (read-only cfg_* fields)
    snprintf(cmd, sizeof(cmd), "cfg_t.txt=\"%d\"", CONFIG_NEXTION_MAX_OPERATIONAL_TIME_MIN);
    nextion_send_cmd(cmd);
    snprintf(cmd, sizeof(cmd), "cfg_Tmax.txt=\"%d\"", CONFIG_NEXTION_MAX_TEMPERATURE_C);
    nextion_send_cmd(cmd);
    snprintf(cmd, sizeof(cmd), "cfg_dt.txt=\"%d\"", CONFIG_NEXTION_SENSOR_READ_FREQUENCY_SEC);
    nextion_send_cmd(cmd);
    // Format delta_t_max as decimal (x10 value)
    char delta_max_buf[16];
    format_delta_x10(CONFIG_NEXTION_DELTA_T_MAX_PER_MIN_X10, delta_max_buf, sizeof(delta_max_buf));
    snprintf(cmd, sizeof(cmd), "cfg_dTmax.txt=\"%s\"", delta_max_buf);
    nextion_send_cmd(cmd);
    snprintf(cmd, sizeof(cmd), "cfg_Power.txt=\"%d\"", CONFIG_NEXTION_HEATER_POWER_KW);
    nextion_send_cmd(cmd);

    // Note: Time fields are populated by Nextion from its built-in RTC in preinit

    LOGGER_LOG_INFO(TAG, "Settings init sent");
}

// Handle save settings: RTC time/date only (user config removed)
static void handle_save_settings(const char *payload)
{
    if (!payload) {
        LOGGER_LOG_ERROR(TAG, "save_settings: payload is NULL");
        return;
    }

    LOGGER_LOG_INFO(TAG, "save_settings raw payload: [%s]", payload);
    LOGGER_LOG_INFO(TAG, "save_settings payload length: %d", (int)strlen(payload));

    // Payload format: timeDirty,dateDirty,hour,min,sec,day,month,year
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

    // Parse dirty flags (first two tokens)
    int time_dirty = atoi(tokens[0]);
    int date_dirty = atoi(tokens[1]);

    // Parse time/date values
    int hour = atoi(tokens[2]);
    int min = atoi(tokens[3]);
    int sec = atoi(tokens[4]);
    int day = atoi(tokens[5]);
    int month = atoi(tokens[6]);
    int year = atoi(tokens[7]);

    // Update RTC time if user edited time fields
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

    // Update RTC date if user edited date fields
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

// Handle prog_page_data: sync current page fields to draft, then navigate
static void handle_prog_page_data(const char *payload)
{
    if (!payload) {
        return;
    }

    // Parse direction and current page data
    // Format: prev/next,name,bStg1,t1,target1,tdelta1,tempdelta1,...
    char buffer[512];
    strncpy(buffer, payload, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    // First token is direction
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

    // Parse remaining tokens (same format as save_prog)
    #define PAGE_DATA_TOKEN_COUNT (1 + PROGRAMS_PAGE_STAGE_COUNT * 5)
    char *tokens[PAGE_DATA_TOKEN_COUNT] = {0};
    size_t token_count = 0;

    char *cursor = rest;
    while (cursor && token_count < PAGE_DATA_TOKEN_COUNT) {
        char *c = strchr(cursor, ',');
        if (c) {
            *c = '\0';
        }
        tokens[token_count++] = cursor;
        cursor = c ? c + 1 : NULL;
    }

    if (token_count < PAGE_DATA_TOKEN_COUNT) {
        // Not enough tokens, just navigate without sync
        if (go_prev) {
            programs_page_apply((uint8_t)(s_programs_page - 1));
        } else {
            programs_page_apply((uint8_t)(s_programs_page + 1));
        }
        return;
    }

    // Save name
    program_draft_set_name(trim_in_place(tokens[0]));

    // Parse and store current page's 5 stages
    size_t idx = 1;
    for (int row = 0; row < PROGRAMS_PAGE_STAGE_COUNT; ++row) {
        uint8_t stage_num = (uint8_t)((s_programs_page - 1) * PROGRAMS_PAGE_STAGE_COUNT + row + 1);
        
        int t_min = 0;
        int target_t = 0;
        int t_delta = 0;
        int delta_t_x10 = 0;
        bool t_set = false;
        bool target_set = false;
        bool t_delta_set = false;
        bool delta_t_set = false;

        idx++; // skip bStg label

        parse_optional_int(tokens[idx++], &t_min, &t_set);
        parse_optional_int(tokens[idx++], &target_t, &target_set);
        parse_optional_int(tokens[idx++], &t_delta, &t_delta_set);
        parse_optional_delta_x10(tokens[idx++], &delta_t_x10, &delta_t_set);

        bool any_set = t_set || target_set || t_delta_set || delta_t_set;
        if (!any_set) {
            program_draft_clear_stage(stage_num);
            continue;
        }

        if (!t_delta_set) {
            t_delta = CONFIG_NEXTION_T_DELTA_MIN_MIN;
        }

        program_draft_set_stage(stage_num,
                                t_min,
                                target_t,
                                t_delta,
                                delta_t_x10,
                                t_set,
                                target_set,
                                t_delta_set,
                                delta_t_set);
    }

    // Navigate to the new page first (updates UI immediately)
    if (go_prev) {
        programs_page_apply((uint8_t)(s_programs_page - 1));
    } else {
        programs_page_apply((uint8_t)(s_programs_page + 1));
    }

    // Sync the buffer
    sync_program_buffer();
}

static void handle_add_prog(void)
{
    s_original_program_name[0] = '\0';
    program_draft_clear();
    s_programs_page = 1;
    s_graph_visible = false;
    nextion_send_cmd("page " CONFIG_NEXTION_PAGE_PROGRAMS);
    vTaskDelay(pdMS_TO_TICKS(30));
    programs_page_apply(1);
    nextion_send_cmd("progNameInput.txt=\"\"");
    sync_program_buffer();
}

static void handle_delete_prog(const char *current_name)
{
    // Check if we're in edit mode (original name is set)
    if (s_original_program_name[0] == '\0') {
        nextion_show_error("Open a program with Edit first");
        return;
    }

    // Check if name matches original
    const char *name = current_name ? trim_in_place((char *)current_name) : "";
    if (strcmp(name, s_original_program_name) != 0) {
        nextion_show_error("Restore original name to delete");
        return;
    }

    // Show confirmation dialog - use escaped quotes for Nextion string
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "confirmTxt.txt=\"Delete \\\"%.26s\\\"?\"", s_original_program_name);
    LOGGER_LOG_INFO(TAG, "Delete confirm cmd: %s", cmd);
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

static void handle_confirm_delete(void)
{
    // Hide confirmation UI first
    nextion_send_cmd("vis confirmBdy,0");
    nextion_send_cmd("vis confirmTxt,0");
    nextion_send_cmd("vis confirmDelete,0");
    nextion_send_cmd("vis confirmCancel,0");

    // Perform deletion
    char error_msg[64];
    if (!nextion_storage_delete_program(s_original_program_name, error_msg, sizeof(error_msg))) {
        nextion_show_error(error_msg);
        return;
    }

    // Clear state and UI
    s_original_program_name[0] = '\0';
    program_draft_clear();
    s_programs_page = 1;
    programs_page_apply(1);
    nextion_send_cmd("progNameInput.txt=\"\"");
    sync_program_buffer();
}

static void handle_edit_prog(const char *payload)
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

    if (!nextion_file_exists(path)) {
        nextion_show_error("Program not found");
        return;
    }

    char error_msg[64];
    if (!load_program_into_draft(name, error_msg, sizeof(error_msg))) {
        nextion_show_error(error_msg);
        return;
    }

    // Store original name to allow overwriting only this file
    strncpy(s_original_program_name, program_draft_get()->name, sizeof(s_original_program_name) - 1);
    s_original_program_name[sizeof(s_original_program_name) - 1] = '\0';
    s_programs_page = 1;
    s_graph_visible = false;

    nextion_send_cmd("page " CONFIG_NEXTION_PAGE_PROGRAMS);
    vTaskDelay(pdMS_TO_TICKS(30));
    programs_page_apply(1);

    char cmd[96];
    snprintf(cmd, sizeof(cmd), "progNameInput.txt=\"%s\"", program_draft_get()->name);
    nextion_send_cmd(cmd);

    sync_program_buffer();
}

static void handle_program_select(const char *filename)
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

    const ProgramDraft *parsed = program_draft_get();
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "progNameDisp.txt=\"%s\"", parsed->name);
    nextion_send_cmd(cmd);

    int total_time = 0;
    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        const ProgramStage *stage = &parsed->stages[i];
        if (!stage->is_set) {
            continue;
        }
        total_time += stage->t_min;
    }

    LOGGER_LOG_INFO(TAG, "Program parsed: name=%s time=%d", parsed->name, total_time);

    snprintf(cmd, sizeof(cmd), "timeElapsed.txt=\"0\"");
    nextion_send_cmd(cmd);
    snprintf(cmd, sizeof(cmd), "timeRamaining.txt=\"%d\"", total_time);
    nextion_send_cmd(cmd);

    // Allocate graph samples on heap
    uint8_t *samples = malloc(CONFIG_NEXTION_PROGRAMS_GRAPH_WIDTH);
    if (!samples) {
        nextion_show_error("Out of memory");
        return;
    }

    size_t count = program_build_graph(parsed, samples, CONFIG_NEXTION_PROGRAMS_GRAPH_WIDTH, CONFIG_NEXTION_MAIN_GRAPH_WIDTH, CONFIG_NEXTION_MAX_TEMPERATURE_C, program_get_current_temp_c());
    if (count == 0) {
        nextion_show_error("Graph build failed");
        free(samples);
        return;
    }

    snprintf(cmd, sizeof(cmd), "cle %d,0", CONFIG_NEXTION_GRAPH_DISP_ID);
    nextion_send_cmd(cmd);
    for (size_t i = 0; i < count; ++i) {
        snprintf(cmd, sizeof(cmd), "add %d,0,%u", CONFIG_NEXTION_GRAPH_DISP_ID, (unsigned)samples[i]);
        nextion_send_cmd(cmd);
        if ((i % 64) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    free(samples);
    sync_program_buffer();
}

void nextion_event_handle_line(const char *line)
{
    if (!line || line[0] == '\0') {
        return;
    }

    // Buffer for cleaned line (Method 2: name + 5 stages * 5 fields = ~300 bytes max)
    char clean[512];
    size_t idx = 0;
    for (size_t i = 0; line[i] != '\0' && idx + 1 < sizeof(clean); ++i) {
        unsigned char c = (unsigned char)line[i];
        if (c >= 32 && c <= 126) {
            clean[idx++] = (char)c;
        }
    }
    clean[idx] = '\0';

    const char *nav = strstr(clean, "nav:");
    if (nav) {
        handle_nav_event(nav + 4);
        return;
    }

    if (strstr(clean, "prog_start")) {
        handle_run_start();
        return;
    }

    if (strstr(clean, "prog_pause")) {
        handle_run_pause();
        return;
    }

    if (strstr(clean, "prog_stop")) {
        handle_run_stop();
        return;
    }

    const char *prog_sel = strstr(clean, "prog_select:");
    if (prog_sel) {
        LOGGER_LOG_INFO(TAG, "Program select raw: %s", prog_sel + 12);
        handle_program_select(prog_sel + 12);
        return;
    }

    const char *page_data = strstr(clean, "prog_page_data:");
    if (page_data) {
        handle_prog_page_data(page_data + 15);
        return;
    }

    if (strstr(clean, "add_prog")) {
        handle_add_prog();
        return;
    }

    const char *edit_prog = strstr(clean, "edit_prog:");
    if (edit_prog) {
        handle_edit_prog(edit_prog + 10);
        return;
    }

    // Simple page navigation (no data sync - for backward compatibility)
    if (strstr(clean, "prog_page:prev")) {
        programs_page_apply((uint8_t)(s_programs_page - 1));
        return;
    }

    if (strstr(clean, "prog_page:next")) {
        programs_page_apply((uint8_t)(s_programs_page + 1));
        return;
    }

    const char *save = strstr(clean, "save_prog:");
    if (save) {
        handle_save_prog(save + 10);
        return;
    }

    const char *delete_prog = strstr(clean, "delete_prog:");
    if (delete_prog) {
        handle_delete_prog(delete_prog + 12);
        return;
    }

    if (strstr(clean, "confirm_delete")) {
        handle_confirm_delete();
        return;
    }

    const char *show_graph = strstr(clean, "show_graph:");
    if (show_graph) {
        handle_show_graph(show_graph + 11);
        return;
    }

    const char *autofill = strstr(clean, "autofill:");
    if (autofill) {
        handle_autofill(autofill + 9);
        return;
    }

    // Settings page init event
    if (strstr(clean, "settings_init")) {
        handle_settings_init();
        return;
    }

    // Save settings event
    const char *save_settings = strstr(clean, "save_settings:");
    if (save_settings) {
        handle_save_settings(save_settings + 14);
        return;
    }

    if (strstr(clean, "err:close")) {
        nextion_clear_error();
        return;
    }

    LOGGER_LOG_INFO(TAG, "Unhandled Nextion line: %s", clean);
}

/* ── Phase 4: event-driven display handlers ────────────────────────
 *
 * These are called from the HMI coordinator task (never from the
 * event-loop task), so they are fully serialized with line handlers
 * and can safely touch shared state and send Nextion commands.
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

    // Live waveform: add measured-temp point on channel 1
    if (s_waveform_active && s_waveform_x < (uint32_t)CONFIG_NEXTION_MAIN_GRAPH_WIDTH) {
        // Scale temperature to graph pixel (0 = bottom, HEIGHT = top)
        int y = 0;
        if (CONFIG_NEXTION_MAX_TEMPERATURE_C > 0) {
            y = (temp_c * CONFIG_NEXTION_MAIN_GRAPH_HEIGHT) / CONFIG_NEXTION_MAX_TEMPERATURE_C;
        }
        if (y < 0) y = 0;
        if (y > CONFIG_NEXTION_MAIN_GRAPH_HEIGHT) y = CONFIG_NEXTION_MAIN_GRAPH_HEIGHT;

        snprintf(cmd, sizeof(cmd), "add %d,1,%d",
                 CONFIG_NEXTION_GRAPH_DISP_ID, y);
        nextion_send_cmd(cmd);
        s_waveform_x++;
    }
}

void nextion_event_handle_profile_started(void)
{
    LOGGER_LOG_INFO(TAG, "Profile started — updating display");
    nextion_send_cmd("progNameDisp.txt=\"Running\"");
    nextion_clear_error();

    // Compute total profile duration for waveform pacing
    const ProgramDraft *draft = program_draft_get();
    uint32_t total_min = 0;
    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        if (draft->stages[i].is_set) {
            total_min += (uint32_t)draft->stages[i].t_min;
        }
    }
    s_waveform_total_ms = total_min * 60U * 1000U;
    s_waveform_x = 0;
    s_waveform_active = true;

    // Clear channel 1 (live measured temp) — channel 0 was pre-rendered
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "cle %d,1", CONFIG_NEXTION_GRAPH_DISP_ID);
    nextion_send_cmd(cmd);
}

void nextion_event_handle_profile_paused(void)
{
    LOGGER_LOG_INFO(TAG, "Profile paused");
    nextion_send_cmd("progNameDisp.txt=\"Paused\"");
}

void nextion_event_handle_profile_resumed(void)
{
    LOGGER_LOG_INFO(TAG, "Profile resumed");
    nextion_send_cmd("progNameDisp.txt=\"Running\"");
}

void nextion_event_handle_profile_stopped(void)
{
    LOGGER_LOG_INFO(TAG, "Profile stopped");
    nextion_send_cmd("progNameDisp.txt=\"Stopped\"");
    s_waveform_active = false;
}

static const char *coordinator_error_to_str(coordinator_error_code_t code)
{
    switch (code) {
        case COORDINATOR_ERROR_NONE:               return "Unknown error";
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

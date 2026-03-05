#include "heating_program_types.h"
#include "heating_program_models_internal.h"
#include "nextion_hmi.h"
#include "logger_component.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "program_models";

static ProgramDraft s_program_draft;
static ProgramDraft s_run_program;   // Snapshot copied at run-start, read by coordinator
static int s_current_temp_c = 23;
static float s_current_temp_f = 23.0f;
static int s_ambient_temp_c = 23;    // User-settable ambient temp, persisted to NVS
static int s_current_kw = 0;
static SemaphoreHandle_t s_program_mutex = NULL;

#define NVS_NAMESPACE "user_prefs"
#define NVS_KEY_AMBIENT "ambient_c"

void program_models_init(void)
{
    if (s_program_mutex == NULL) {
        s_program_mutex = xSemaphoreCreateRecursiveMutex();
        configASSERT(s_program_mutex);
    }

    /* Load ambient temperature from NVS (default 23 if not set) */
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t val = 23;
        if (nvs_get_i32(nvs, NVS_KEY_AMBIENT, &val) == ESP_OK) {
            s_ambient_temp_c = (int)val;
            LOGGER_LOG_INFO(TAG, "Loaded ambient temp from NVS: %d", s_ambient_temp_c);
        }
        nvs_close(nvs);
    }
}

void program_draft_clear(void)
{
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    memset(&s_program_draft, 0, sizeof(s_program_draft));
    xSemaphoreGiveRecursive(s_program_mutex);
}

void program_draft_set_name(const char *name)
{
    if (!name) {
        return;
    }
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    strncpy(s_program_draft.name, name, sizeof(s_program_draft.name) - 1);
    s_program_draft.name[sizeof(s_program_draft.name) - 1] = '\0';
    xSemaphoreGiveRecursive(s_program_mutex);
}

bool program_draft_set_stage(uint8_t stage_number,
                             int t_min,
                             int target_t_c,
                             int t_delta_min,
                             int delta_t_per_min_x10,
                             bool t_set,
                             bool target_set,
                             bool t_delta_set,
                             bool delta_t_set)
{
    if (stage_number < 1 || stage_number > PROGRAMS_TOTAL_STAGE_COUNT) {
        return false;
    }

    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);

    ProgramStage *stage = &s_program_draft.stages[stage_number - 1];
    stage->t_min = t_min;
    stage->target_t_c = target_t_c;
    stage->t_delta_min = t_delta_min;
    stage->delta_t_per_min_x10 = delta_t_per_min_x10;
    stage->t_set = t_set;
    stage->target_set = target_set;
    stage->t_delta_set = t_delta_set;
    stage->delta_t_set = delta_t_set;
    stage->is_set = t_set || target_set || t_delta_set || delta_t_set;
    xSemaphoreGiveRecursive(s_program_mutex);

    return true;
}

void program_draft_clear_stage(uint8_t stage_number)
{
    if (stage_number < 1 || stage_number > PROGRAMS_TOTAL_STAGE_COUNT) {
        return;
    }
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    memset(&s_program_draft.stages[stage_number - 1], 0, sizeof(ProgramStage));
    xSemaphoreGiveRecursive(s_program_mutex);
}

void program_draft_get(ProgramDraft *out)
{
    if (!out) {
        return;
    }
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    memcpy(out, &s_program_draft, sizeof(*out));
    xSemaphoreGiveRecursive(s_program_mutex);
}

const char *program_draft_get_name(void)
{
    static char name_buf[sizeof(((ProgramDraft *)0)->name)];
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    memcpy(name_buf, s_program_draft.name, sizeof(name_buf));
    xSemaphoreGiveRecursive(s_program_mutex);
    return name_buf;
}

void program_set_current_temp_c(int temp_c)
{
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    s_current_temp_c = temp_c;
    xSemaphoreGiveRecursive(s_program_mutex);
}

int program_get_current_temp_c(void)
{
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    int value = s_current_temp_c;
    xSemaphoreGiveRecursive(s_program_mutex);
    return value;
}

void program_set_current_temp_f(float temp_f)
{
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    s_current_temp_f = temp_f;
    s_current_temp_c = (int)(temp_f + 0.5f);
    xSemaphoreGiveRecursive(s_program_mutex);
}

float program_get_current_temp_f(void)
{
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    float value = s_current_temp_f;
    xSemaphoreGiveRecursive(s_program_mutex);
    return value;
}

void program_set_ambient_temp_c(int temp_c)
{
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    s_ambient_temp_c = temp_c;
    xSemaphoreGiveRecursive(s_program_mutex);

    /* Persist to NVS */
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_i32(nvs, NVS_KEY_AMBIENT, (int32_t)temp_c);
        nvs_commit(nvs);
        nvs_close(nvs);
        LOGGER_LOG_INFO(TAG, "Saved ambient temp to NVS: %d", temp_c);
    }
}

int program_get_ambient_temp_c(void)
{
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    int value = s_ambient_temp_c;
    xSemaphoreGiveRecursive(s_program_mutex);
    return value;
}

void program_set_current_kw(int kw)
{
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    s_current_kw = kw;
    xSemaphoreGiveRecursive(s_program_mutex);
}

int program_get_current_kw(void)
{
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    int value = s_current_kw;
    xSemaphoreGiveRecursive(s_program_mutex);
    return value;
}

// ============================================================================
// Run slot — used by coordinator during program execution
// ============================================================================

void hmi_get_run_program(ProgramDraft *out)
{
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    memcpy(out, &s_run_program, sizeof(*out));
    xSemaphoreGiveRecursive(s_program_mutex);
}

void program_copy_draft_to_run_slot(void)
{
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    memcpy(&s_run_program, &s_program_draft, sizeof(s_run_program));
    xSemaphoreGiveRecursive(s_program_mutex);
}

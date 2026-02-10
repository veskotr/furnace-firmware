#include "program_models.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static ProgramDraft s_program_draft;
static int s_current_temp_c = 23;
static int s_current_kw = 0;
static SemaphoreHandle_t s_program_mutex = NULL;

static void ensure_program_mutex(void)
{
    if (s_program_mutex == NULL) {
        s_program_mutex = xSemaphoreCreateRecursiveMutex();
    }
}

void program_draft_clear(void)
{
    ensure_program_mutex();
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    memset(&s_program_draft, 0, sizeof(s_program_draft));
    xSemaphoreGiveRecursive(s_program_mutex);
}

void program_draft_set_name(const char *name)
{
    if (!name) {
        return;
    }
    ensure_program_mutex();
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

    ensure_program_mutex();
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
    ensure_program_mutex();
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    memset(&s_program_draft.stages[stage_number - 1], 0, sizeof(ProgramStage));
    xSemaphoreGiveRecursive(s_program_mutex);
}

const ProgramDraft *program_draft_get(void)
{
    static ProgramDraft snapshot;

    ensure_program_mutex();
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    memcpy(&snapshot, &s_program_draft, sizeof(snapshot));
    xSemaphoreGiveRecursive(s_program_mutex);

    return &snapshot;
}

void program_set_current_temp_c(int temp_c)
{
    ensure_program_mutex();
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    s_current_temp_c = temp_c;
    xSemaphoreGiveRecursive(s_program_mutex);
}

int program_get_current_temp_c(void)
{
    ensure_program_mutex();
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    int value = s_current_temp_c;
    xSemaphoreGiveRecursive(s_program_mutex);
    return value;
}

void program_set_current_kw(int kw)
{
    ensure_program_mutex();
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    s_current_kw = kw;
    xSemaphoreGiveRecursive(s_program_mutex);
}

int program_get_current_kw(void)
{
    ensure_program_mutex();
    xSemaphoreTakeRecursive(s_program_mutex, portMAX_DELAY);
    int value = s_current_kw;
    xSemaphoreGiveRecursive(s_program_mutex);
    return value;
}

#include "program_profile_adapter.h"

#include <string.h>
#include <stdio.h>

#include "app_config.h"

#define HMI_PROFILE_SLOT_COUNT 1

static heating_profile_t s_profiles[HMI_PROFILE_SLOT_COUNT];
static heating_node_t s_nodes[PROGRAMS_TOTAL_STAGE_COUNT];
static char s_profile_name[64];

heating_profile_t *hmi_get_profile_slots(size_t *out_count)
{
    if (out_count) {
        *out_count = HMI_PROFILE_SLOT_COUNT;
    }
    return s_profiles;
}

static void clear_nodes(void)
{
    memset(s_nodes, 0, sizeof(s_nodes));
}

bool hmi_build_profile_from_draft(const ProgramDraft *draft, char *error_msg, size_t error_len)
{
    if (!draft) {
        return false;
    }

    clear_nodes();

    size_t node_count = 0;
    heating_node_t *prev = NULL;

    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        const ProgramStage *stage = &draft->stages[i];
        if (!stage->is_set) {
            continue;
        }

        if (node_count >= PROGRAMS_TOTAL_STAGE_COUNT) {
            return false;
        }

        heating_node_t *node = &s_nodes[node_count++];
        node->type = NODE_TYPE_LINEAR;
        node->set_temp = (float)stage->target_t_c;
        node->duration_ms = (uint32_t)stage->t_min * 60U * 1000U;
        node->previous_node = prev;
        node->next_node = NULL;

        if (prev) {
            prev->next_node = node;
        }
        prev = node;
    }

    if (node_count == 0) {
        if (error_msg && error_len > 0) {
            snprintf(error_msg, error_len, "No stages set");
        }
        return false;
    }

    memset(s_profile_name, 0, sizeof(s_profile_name));
    strncpy(s_profile_name, draft->name, sizeof(s_profile_name) - 1);

    s_profiles[0].name = s_profile_name;
    s_profiles[0].first_node = &s_nodes[0];

    return true;
}

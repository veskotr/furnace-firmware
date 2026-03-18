#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"

// ============================================================================
// Program data model — shared across all components
// ============================================================================
// These types define the furnace heating program format. A program consists of
// a name and an array of stages. Each stage specifies a target temperature,
// duration, and heating rate. This is the single source of truth for program
// data; there is no separate "runtime" model.
//
// Originally lived in nextion_hmi. Moved to common so that coordinator and
// temperature_profile_controller can consume programs directly without
// depending on the HMI component.
// ============================================================================

// Program editor constraints — values from Kconfig
#define PROGRAMS_PAGE_STAGE_COUNT  CONFIG_NEXTION_PROGRAMS_PAGE_STAGE_COUNT
#define PROGRAMS_PAGE_COUNT        CONFIG_NEXTION_PROGRAMS_PAGE_COUNT
#define PROGRAMS_TOTAL_STAGE_COUNT (PROGRAMS_PAGE_STAGE_COUNT * PROGRAMS_PAGE_COUNT)

typedef struct {
    int t_min;                  // Duration in minutes
    int target_t_c;             // Target temperature in °C
    int t_delta_min;            // Delta time in minutes (derived)
    int delta_t_per_min_x10;    // Heating rate x10 fixed-point: 15 = 1.5°C/min
    bool t_set;
    bool target_set;
    bool t_delta_set;
    bool delta_t_set;
    bool is_set;                // True if this stage slot is occupied
} ProgramStage;

typedef struct {
    char name[32];
    ProgramStage stages[PROGRAMS_TOTAL_STAGE_COUNT];
} ProgramDraft;

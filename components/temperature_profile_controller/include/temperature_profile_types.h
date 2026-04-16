#pragma once

#include "core_types.h"

typedef enum
{
    PROFILE_CONTROLLER_ERROR_NONE = 0,
    PROFILE_CONTROLLER_ERROR_INVALID_ARG,
    PROFILE_CONTROLLER_ERROR_NO_PROFILE_LOADED,
    PROFILE_CONTROLLER_ERROR_COMPUTATION_FAILED,
    PROFILE_CONTROLLER_ERROR_TIME_EXCEEDS_PROFILE_DURATION,
    PROFILE_CONTROLLER_ERROR_THRESHOLD_EXCEEDED,
    PROFILE_CONTROLLER_ERROR_UNKNOWN
} profile_controller_error_t;

typedef struct {
    float initial_temperature;
    const program_draft_t *program;   // Program to execute (array of stages)
    int cooldown_rate_x10;         // User-configured cooldown rate (x10)
} temp_profile_config_t;

/**
 * @brief Phase of the current stage (auto-detected from start/target temps).
 */
typedef enum {
    STAGE_PHASE_HEATING,    ///< Ramping up: target > start. Advance when temp reached.
    STAGE_PHASE_HOLDING,    ///< Holding: target ≈ start. Advance when time expires.
    STAGE_PHASE_COOLING,    ///< Ramping down: target < start. Advance when temp reached.
    STAGE_PHASE_COOLDOWN,   ///< All stages done, heater off, fan on
    STAGE_PHASE_COMPLETE    ///< Profile fully complete
} stage_phase_t;

/**
 * @brief Result of a single profile_tick() call.
 */
typedef struct {
    int          current_stage_index;  ///< 0..N-1 for active stages, -1 if cooldown/complete
    stage_phase_t phase;               ///< Current phase of the active stage
    float        setpoint;             ///< PID target temperature for this tick
    bool         stage_changed;        ///< True on the tick a new stage begins
    bool         profile_complete;     ///< True when cooldown is done + temp below threshold
    bool         threshold_violation;  ///< True if temp exceeded setpoint + overshoot threshold
} profile_tick_result_t;

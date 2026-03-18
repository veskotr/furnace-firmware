#ifndef TEMPERATURE_PROFILE_TYPES_H
#define TEMPERATURE_PROFILE_TYPES_H
#include "core_types.h"

typedef enum
{
    PROFILE_CONTROLLER_ERROR_NONE = 0,
    PROFILE_CONTROLLER_ERROR_INVALID_ARG,
    PROFILE_CONTROLLER_ERROR_NO_PROFILE_LOADED,
    PROFILE_CONTROLLER_ERROR_COMPUTATION_FAILED,
    PROFILE_CONTROLLER_ERROR_TIME_EXCEEDS_PROFILE_DURATION,
    PROFILE_CONTROLLER_ERROR_UNKNOWN
} profile_controller_error_t;

typedef struct {
    float initial_temperature;
    const program_draft_t *program;   // Program to execute (array of stages)
} temp_profile_config_t;

/**
 * @brief Phase of the current stage.
 */
typedef enum {
    STAGE_PHASE_RAMPING,    ///< Interpolating from start temp to target over t_min
    STAGE_PHASE_HOLDING,    ///< Target temp reached early, waiting for time to elapse
    STAGE_PHASE_SETTLE,     ///< Time elapsed, temp within tolerance, trying to converge
    STAGE_PHASE_EXTEND,     ///< Time elapsed, temp NOT within tolerance, extending
    STAGE_PHASE_COOLDOWN,   ///< All stages done, implicit cooldown to 0 C
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
    bool         extension_warning;    ///< True if a stage hit max extension and was forced to advance
} profile_tick_result_t;

#endif // TEMPERATURE_PROFILE_TYPES_H
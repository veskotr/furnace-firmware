#include "temperature_profile_controller.h"
#include "sdkconfig.h"
#include "logger_component.h"
#include <math.h>
#include <string.h>

static const char *TAG = "TEMP_PROFILE_CONTROLLER";

typedef struct {
    float initial_temperature;
    const program_draft_t *program;    // Points to the ProgramDraft being executed
    int cooldown_rate_x10;          // User-configured cooldown rate (x10)
} temperature_profile_controller_context_t;

static temperature_profile_controller_context_t *g_temp_profile_controller_ctx = NULL;

/* ── Stateful tick state ──────────────────────────────────────────── */

/**
 * @brief Internal state for the profile_tick() state machine.
 *
 * Tracks which stage we're in, how long we've been in it, what phase
 * the stage is in, and the actual temperature at the start of the stage
 * (so we can ramp from reality, not from the plan).
 */
typedef struct {
    bool         initialized;

    /* Stage tracking */
    int          stage_index;           ///< Index into stages[] of current stage (-1 = build list first time)
    int          active_stages[PROGRAMS_TOTAL_STAGE_COUNT]; ///< Indices of is_set stages
    int          num_active_stages;     ///< Count of active stages
    int          active_pos;            ///< Position in active_stages[] (0..num_active_stages-1)

    /* Time tracking within current stage */
    uint32_t     stage_elapsed_ms;      ///< Time spent in current stage
    uint32_t     stage_planned_ms;      ///< t_min * 60000 for current stage
    uint32_t     overtime_ms;           ///< Time past t_min (for settle/extend phases)

    /* Phase */
    stage_phase_t phase;

    /* Temperatures */
    float        stage_start_temp;      ///< Actual measured temp when stage began
    float        stage_target_temp;     ///< target_t_c of current stage

    /* Cooldown tracking */
    float        cooldown_start_temp;   ///< Temp when cooldown began
    uint32_t     cooldown_elapsed_ms;   ///< Time spent in cooldown
    uint32_t     cooldown_total_ms;     ///< Predicted cooldown duration

    /* Warning flag (latched per stage, cleared on stage advance) */
    bool         extension_warning;
} profile_tick_state_t;

static profile_tick_state_t s_tick = {0};

profile_controller_error_t load_heating_profile(const temp_profile_config_t config)
{
    if (config.program == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid program (NULL)");
        return PROFILE_CONTROLLER_ERROR_INVALID_ARG;
    }

    if (g_temp_profile_controller_ctx == NULL)
    {
        g_temp_profile_controller_ctx = calloc(1, sizeof(temperature_profile_controller_context_t));
        if (g_temp_profile_controller_ctx == NULL)
        {
            LOGGER_LOG_ERROR(TAG, "Failed to allocate temperature profile controller context");
            return PROFILE_CONTROLLER_ERROR_UNKNOWN;
        }
    }

    g_temp_profile_controller_ctx->program = config.program;
    g_temp_profile_controller_ctx->initial_temperature = config.initial_temperature;
    g_temp_profile_controller_ctx->cooldown_rate_x10 = config.cooldown_rate_x10;

    /* Reset tick state for new profile */
    profile_tick_reset();

    return PROFILE_CONTROLLER_ERROR_NONE;
}

/* ── Legacy time-based interpolation (for graph rendering) ────────── */

profile_controller_error_t get_target_temperature_at_time(const uint32_t time_ms, float *temperature)
{
    if (g_temp_profile_controller_ctx == NULL || g_temp_profile_controller_ctx->program == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Program not loaded or invalid argument");
        return PROFILE_CONTROLLER_ERROR_NO_PROFILE_LOADED;
    }

    const program_draft_t *prog = g_temp_profile_controller_ctx->program;
    uint32_t elapsed_time = 0;
    float start_temp = g_temp_profile_controller_ctx->initial_temperature;

    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i)
    {
        const program_stage_t *stage = &prog->stages[i];
        if (!stage->is_set)
        {
            continue;
        }

        uint32_t stage_duration_ms = (uint32_t)stage->t_min * 60U * 1000U;

        if (time_ms <= elapsed_time + stage_duration_ms)
        {
            // Linear interpolation within this stage
            float t = (float)(time_ms - elapsed_time) / (float)stage_duration_ms;
            *temperature = start_temp + ((float)stage->target_t_c - start_temp) * t;
            return PROFILE_CONTROLLER_ERROR_NONE;
        }

        elapsed_time += stage_duration_ms;
        start_temp = (float)stage->target_t_c;
    }

    /* Implicit cooldown: ramp from last stage temp to 0 C */
    int cd_rate = g_temp_profile_controller_ctx->cooldown_rate_x10;
    if (start_temp > 0.0f && cd_rate > 0) {
        float cooldown_min = (start_temp * 10.0f) / (float)cd_rate;
        if (cooldown_min < 1.0f) {
            cooldown_min = 1.0f;
        }
        uint32_t cooldown_ms = (uint32_t)(cooldown_min * 60.0f * 1000.0f);

        if (time_ms <= elapsed_time + cooldown_ms) {
            float t = (float)(time_ms - elapsed_time) / (float)cooldown_ms;
            *temperature = start_temp * (1.0f - t);
            return PROFILE_CONTROLLER_ERROR_NONE;
        }
        elapsed_time += cooldown_ms;
    }

    /* Past cooldown — hold at 0 */
    *temperature = 0.0f;
    return PROFILE_CONTROLLER_ERROR_NONE;
}

/* ── Stateful profile tick ────────────────────────────────────────── */

void profile_tick_reset(void)
{
    memset(&s_tick, 0, sizeof(s_tick));
    s_tick.stage_index = -1;
    s_tick.active_pos  = -1;
}

/**
 * @brief Build the ordered list of active stage indices once.
 */
static void build_active_stage_list(const program_draft_t *prog)
{
    s_tick.num_active_stages = 0;
    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        if (prog->stages[i].is_set) {
            s_tick.active_stages[s_tick.num_active_stages++] = i;
        }
    }
}

/**
 * @brief Auto-detect stage phase from start and target temperatures.
 */
static stage_phase_t detect_stage_phase(float start_temp, float target_temp, float tolerance)
{
    if (target_temp > start_temp + tolerance) {
        return STAGE_PHASE_HEATING;
    } else if (target_temp < start_temp - tolerance) {
        return STAGE_PHASE_COOLING;
    } else {
        return STAGE_PHASE_HOLDING;
    }
}

/**
 * @brief Advance to the next stage (or cooldown if no more stages).
 */
static void advance_stage(float current_temp, profile_tick_result_t *result)
{
    s_tick.active_pos++;

    if (s_tick.active_pos >= s_tick.num_active_stages) {
        /* All stages done → enter cooldown (heater off, fan on) */
        s_tick.phase = STAGE_PHASE_COOLDOWN;
        s_tick.cooldown_start_temp = current_temp;

        s_tick.stage_index = -1;
        result->stage_changed = true;
        LOGGER_LOG_INFO(TAG, "All stages done, entering cooldown from %.1f C", current_temp);
        return;
    }

    int idx = s_tick.active_stages[s_tick.active_pos];
    const program_draft_t *prog = g_temp_profile_controller_ctx->program;
    const program_stage_t *stage = &prog->stages[idx];

    s_tick.stage_index      = idx;
    s_tick.stage_elapsed_ms = 0;
    s_tick.stage_planned_ms = (uint32_t)stage->t_min * 60U * 1000U;
    s_tick.stage_start_temp = current_temp;  /* Start from ACTUAL temperature */
    s_tick.stage_target_temp = (float)stage->target_t_c;

    const float tolerance = (float)CONFIG_NEXTION_TEMP_TOLERANCE_C;
    s_tick.phase = detect_stage_phase(current_temp, s_tick.stage_target_temp, tolerance);

    result->stage_changed = true;

    const char *phase_str = (s_tick.phase == STAGE_PHASE_HEATING)  ? "HEATING" :
                            (s_tick.phase == STAGE_PHASE_HOLDING)  ? "HOLDING" :
                            (s_tick.phase == STAGE_PHASE_COOLING)  ? "COOLING" : "??";

    LOGGER_LOG_INFO(TAG, "Stage %d started [%s]: %.1f C → %d C over %d min",
                    idx + 1, phase_str, current_temp, stage->target_t_c, stage->t_min);
}

profile_controller_error_t profile_tick(uint32_t elapsed_since_last_ms,
                                        float current_temp,
                                        profile_tick_result_t *result)
{
    if (!g_temp_profile_controller_ctx || !g_temp_profile_controller_ctx->program || !result) {
        return PROFILE_CONTROLLER_ERROR_NO_PROFILE_LOADED;
    }

    const program_draft_t *prog = g_temp_profile_controller_ctx->program;

    /* Initialize result */
    result->current_stage_index = -1;
    result->phase               = STAGE_PHASE_COMPLETE;
    result->setpoint            = 0.0f;
    result->stage_changed       = false;
    result->profile_complete    = false;
    result->threshold_violation = false;

    /* First call: build stage list and enter first stage */
    if (!s_tick.initialized) {
        build_active_stage_list(prog);
        s_tick.initialized = true;
        s_tick.active_pos = -1;   /* advance_stage will increment to 0 */
        advance_stage(current_temp, result);

        if (s_tick.phase == STAGE_PHASE_COOLDOWN) {
            /* No active stages — go straight to cooldown */
            result->setpoint = 0.0f;
            result->phase = STAGE_PHASE_COOLDOWN;
            return PROFILE_CONTROLLER_ERROR_NONE;
        }
    }

    const float tolerance = (float)CONFIG_NEXTION_TEMP_TOLERANCE_C;
    const float overshoot_threshold = (float)CONFIG_COORDINATOR_OVERSHOOT_THRESHOLD_C;

    /* ── Handle cooldown phase ────────────────────────────────────── */
    if (s_tick.phase == STAGE_PHASE_COOLDOWN) {
        /*
         * Cooldown: heater is off, fan is on. Setpoint is 0.
         * We just wait for temp to drop below the completion threshold.
         * TODO: Update with actual cooldown rate data from hardware testing.
         */
        result->current_stage_index = -1;
        result->phase = STAGE_PHASE_COOLDOWN;
        result->setpoint = 0.0f;

        if (current_temp < (float)CONFIG_COORDINATOR_PROFILE_COMPLETE_TEMP_C) {
            result->profile_complete = true;
            result->phase = STAGE_PHASE_COMPLETE;
            LOGGER_LOG_INFO(TAG, "Profile complete: temp %.1f C < %d C threshold",
                            current_temp, CONFIG_COORDINATOR_PROFILE_COMPLETE_TEMP_C);
        }

        return PROFILE_CONTROLLER_ERROR_NONE;
    }

    /* ── Handle complete phase (shouldn't tick after this, but be safe) ── */
    if (s_tick.phase == STAGE_PHASE_COMPLETE) {
        result->phase = STAGE_PHASE_COMPLETE;
        result->profile_complete = true;
        result->setpoint = 0.0f;
        return PROFILE_CONTROLLER_ERROR_NONE;
    }

    /* ── Active stage processing ──────────────────────────────────── */
    s_tick.stage_elapsed_ms += elapsed_since_last_ms;

    float target = s_tick.stage_target_temp;
    float setpoint;

    switch (s_tick.phase) {

    case STAGE_PHASE_HEATING:
        /*
         * Linear ramp: setpoint = start + (target - start) * min(1, t/total)
         * Advance when current temp reaches target (within tolerance).
         * If time expires before reaching target, hold setpoint at target
         * and keep waiting — the PID will drive us there.
         */
        if (s_tick.stage_planned_ms > 0) {
            float frac = (float)s_tick.stage_elapsed_ms / (float)s_tick.stage_planned_ms;
            if (frac > 1.0f) frac = 1.0f;
            setpoint = s_tick.stage_start_temp
                     + (target - s_tick.stage_start_temp) * frac;
        } else {
            setpoint = target;
        }

        if (current_temp >= target - tolerance) {
            LOGGER_LOG_INFO(TAG, "Stage %d [HEATING]: target reached (%.1f C >= %.1f C), advancing",
                            s_tick.stage_index + 1, current_temp, target - tolerance);
            advance_stage(current_temp, result);
            if (s_tick.phase == STAGE_PHASE_COOLDOWN) {
                setpoint = 0.0f;
            } else {
                setpoint = s_tick.stage_start_temp;
            }
        }
        break;

    case STAGE_PHASE_HOLDING:
        /*
         * Hold at target temperature for the full stage duration.
         * Advance only when time expires.
         * stage_planned_ms == 0 means this was a cooling stage with ASAP time
         * that landed within tolerance — skip it immediately is correct.
         */
        setpoint = target;

        if (s_tick.stage_planned_ms > 0 &&
            s_tick.stage_elapsed_ms >= s_tick.stage_planned_ms) {
            LOGGER_LOG_INFO(TAG, "Stage %d [HOLDING]: time expired, advancing",
                            s_tick.stage_index + 1);
            advance_stage(current_temp, result);
            if (s_tick.phase == STAGE_PHASE_COOLDOWN) {
                setpoint = 0.0f;
            } else {
                setpoint = s_tick.stage_start_temp;
            }
        } else if (s_tick.stage_planned_ms == 0) {
            /* ASAP / cooling stage that's already at target — advance */
            LOGGER_LOG_INFO(TAG, "Stage %d [HOLDING]: ASAP (t=0), advancing",
                            s_tick.stage_index + 1);
            advance_stage(current_temp, result);
            if (s_tick.phase == STAGE_PHASE_COOLDOWN) {
                setpoint = 0.0f;
            } else {
                setpoint = s_tick.stage_start_temp;
            }
        }
        break;

    case STAGE_PHASE_COOLING:
        /*
         * Cooling: setpoint is the target directly. Time is irrelevant.
         * Advance when current temp drops to target (within tolerance).
         */
        setpoint = target;

        if (current_temp <= target + tolerance) {
            LOGGER_LOG_INFO(TAG, "Stage %d [COOLING]: target reached (%.1f C <= %.1f C), advancing",
                            s_tick.stage_index + 1, current_temp, target + tolerance);
            advance_stage(current_temp, result);
            if (s_tick.phase == STAGE_PHASE_COOLDOWN) {
                setpoint = 0.0f;
            } else {
                setpoint = s_tick.stage_start_temp;
            }
        }
        break;

    default:
        setpoint = 0.0f;
        break;
    }

    result->current_stage_index = s_tick.stage_index;
    result->phase = s_tick.phase;
    result->setpoint = setpoint;

    /* ── Overshoot threshold check ─────────────────────────────────── */
    if (s_tick.phase != STAGE_PHASE_COOLDOWN &&
        s_tick.phase != STAGE_PHASE_COMPLETE &&
        //MAYBE TODO take into account both + and - overshoot thresholds based on stage direction
        current_temp > setpoint + overshoot_threshold) {
        LOGGER_LOG_ERROR(TAG, "OVERSHOOT: temp %.1f C exceeds setpoint %.1f C + %d C threshold!",
                         current_temp, setpoint, CONFIG_COORDINATOR_OVERSHOOT_THRESHOLD_C);
        result->threshold_violation = true;
        return PROFILE_CONTROLLER_ERROR_THRESHOLD_EXCEEDED;
    }

    return PROFILE_CONTROLLER_ERROR_NONE;
}

profile_controller_error_t shutdown_profile_controller(void)
{
    if (g_temp_profile_controller_ctx == NULL)
    {
        return PROFILE_CONTROLLER_ERROR_NONE;
    }

    free(g_temp_profile_controller_ctx);
    g_temp_profile_controller_ctx = NULL;

    /* Reset tick state */
    profile_tick_reset();

    return PROFILE_CONTROLLER_ERROR_NONE;
}

/* ── Live target update (manual mode) ─────────────────────────────── */

profile_controller_error_t profile_update_stage_target(float new_target,
                                                       uint32_t new_planned_ms,
                                                       float current_temp)
{
    if (!s_tick.initialized) {
        return PROFILE_CONTROLLER_ERROR_NO_PROFILE_LOADED;
    }

    /* Only meaningful during an active stage (HEATING, HOLDING, COOLING) */
    if (s_tick.phase == STAGE_PHASE_COOLDOWN ||
        s_tick.phase == STAGE_PHASE_COMPLETE) {
        LOGGER_LOG_WARN(TAG, "update_stage_target ignored — not in active stage (phase=%d)",
                        (int)s_tick.phase);
        return PROFILE_CONTROLLER_ERROR_NONE;
    }

    float old_target = s_tick.stage_target_temp;

    /* Update the stage target and restart from current temp */
    s_tick.stage_target_temp = new_target;
    s_tick.stage_start_temp  = current_temp;
    s_tick.stage_elapsed_ms  = 0;
    s_tick.stage_planned_ms  = new_planned_ms;

    /* Re-detect stage type based on new target vs current temp */
    const float tolerance = (float)CONFIG_NEXTION_TEMP_TOLERANCE_C;
    s_tick.phase = detect_stage_phase(current_temp, new_target, tolerance);

    /* Also update the ProgramDraft stage so status reports are consistent */
    if (g_temp_profile_controller_ctx && g_temp_profile_controller_ctx->program) {
        /* The program pointer points into coordinator_ctx_t.run_program
         * which we're allowed to mutate for manual mode. */
        program_draft_t *prog = (program_draft_t *)g_temp_profile_controller_ctx->program;
        if (s_tick.active_pos >= 0 && s_tick.active_pos < s_tick.num_active_stages) {
            int idx = s_tick.active_stages[s_tick.active_pos];
            prog->stages[idx].target_t_c = (int)new_target;
            prog->stages[idx].t_min      = (int)(new_planned_ms / 60000U);
        }
    }

    LOGGER_LOG_INFO(TAG, "Live target update: %.0f C → %.0f C, ramp from %.1f C over %lu ms",
                    old_target, new_target, current_temp,
                    (unsigned long)new_planned_ms);

    return PROFILE_CONTROLLER_ERROR_NONE;
}

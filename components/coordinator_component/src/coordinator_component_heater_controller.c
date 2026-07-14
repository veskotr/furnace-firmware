#include "utils.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "temperature_profile_controller.h"
#include "pid_component.h"
#include "coordinator_component_types.h"
#include "coordinator_component_internal.h"
#include "event_manager.h"
#include "event_registry.h"
#include "furnace_error_types.h"
#include "error_manager.h"
#include "heater_controller_component.h"

#include <stdatomic.h>
#include <string.h>

#include "commands_dispatcher.h"

static const char* TAG = "COORDINATOR_TASK";

static const health_monitor_data_t coordinator_health_data = {
    .component_id = CONFIG_COORDINATOR_COMPONENT_ID,
    .component_name = "Coordinator",
    .timeout_ticks = pdMS_TO_TICKS(CONFIG_COORDINATOR_HEARTBEAT_TIMEOUT_MS),
};

typedef struct
{
    const char* task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} CoordinatorTaskConfig_t;

static const CoordinatorTaskConfig_t coordinator_task_config = {
    .task_name = CONFIG_COORDINATOR_TASK_NAME,
    .stack_size = CONFIG_COORDINATOR_TASK_STACK_SIZE,
    .task_priority = CONFIG_COORDINATOR_TASK_PRIORITY
};

/**
 * @brief esp_timer callback — wakes the heater controller task at a fixed interval.
 *        Runs in ISR context on ESP32, so only xTaskNotifyGive is safe here.
 */
static void pid_tick_timer_cb(void* arg)
{
    coordinator_ctx_t* ctx = (coordinator_ctx_t*)arg;
    if (ctx->task_handle != NULL)
    {
        xTaskNotifyGive(ctx->task_handle);
    }
}

/**
 * @brief Count is_set stages in the active program (used for "S2/5" displays).
 */
static int8_t count_active_stages(const coordinator_ctx_t *ctx)
{
    if (!ctx->has_program) {
        return 0;
    }
    int8_t n = 0;
    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        if (ctx->run_program.stages[i].is_set) ++n;
    }
    return n;
}

/**
 * @brief Map the profile controller's phase to the wire-format enum.
 */
static uint8_t map_phase(stage_phase_t p)
{
    switch (p) {
        case STAGE_PHASE_HEATING:  return COORD_STAGE_PHASE_HEATING;
        case STAGE_PHASE_HOLDING:  return COORD_STAGE_PHASE_HOLDING;
        case STAGE_PHASE_COOLING:  return COORD_STAGE_PHASE_COOLING;
        case STAGE_PHASE_COOLDOWN: return COORD_STAGE_PHASE_COOLDOWN;
        case STAGE_PHASE_COMPLETE: return COORD_STAGE_PHASE_COMPLETE;
        default:                   return COORD_STAGE_PHASE_COMPLETE;
    }
}

/**
 * @brief Convert profile_tick's stage index into the active-stage ordinal
 *        (0-based position within is_set stages), which is what we display.
 */
static int8_t active_stage_ordinal(const coordinator_ctx_t *ctx, int profile_stage_index)
{
    if (!ctx->has_program || profile_stage_index < 0) return -1;
    int8_t ord = 0;
    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        if (!ctx->run_program.stages[i].is_set) continue;
        if (i == profile_stage_index) return ord;
        ++ord;
    }
    return -1;
}

/**
 * @brief Build and post a coordinator status update event.
 */
static void post_status_update(const coordinator_ctx_t *ctx,
                                const profile_tick_result_t *tick,
                                float power_output)
{
    /* Only HOLDING has a meaningful time-remaining-in-stage. HEATING/COOLING
     * advance by temperature, so their planned t_min is a soft hint at best. */
    uint32_t stage_remaining_ms = 0;
    if (tick->phase == STAGE_PHASE_HOLDING &&
        tick->stage_planned_ms > tick->stage_elapsed_ms) {
        stage_remaining_ms = tick->stage_planned_ms - tick->stage_elapsed_ms;
    }

    coordinator_status_data_t status = {
        .current_temperature = coordinator_get_current_temperature(ctx),
        .target_temperature  = tick->setpoint,
        .power_output        = power_output,
        .elapsed_ms          = ctx->heating_task_state.current_time_elapsed_ms,
        .total_ms            = ctx->heating_task_state.estimated_total_duration_ms,
        .stage_index         = active_stage_ordinal(ctx, tick->current_stage_index),
        .total_active_stages = count_active_stages(ctx),
        .phase               = map_phase(tick->phase),
        .stage_remaining_ms  = stage_remaining_ms,
    };
    post_coordinator_event(COORDINATOR_EVENT_STATUS_UPDATE,
                           &status, sizeof(status));
}

/**
 * @brief When a stage advances before its planned t_min has elapsed,
 *        shrink the estimated total program duration by the unused time
 *        so the HMI's remaining-time display decreases accordingly.
 *
 *        Called after profile_tick when stage_changed is true.
 */
static void update_estimate_on_stage_change(coordinator_ctx_t *ctx,
                                            const profile_tick_result_t *tick)
{
    int prev_idx = ctx->last_profile_stage_index;
    uint32_t now_elapsed = ctx->heating_task_state.current_time_elapsed_ms;

    if (prev_idx >= 0 && prev_idx < PROGRAMS_TOTAL_STAGE_COUNT &&
        ctx->run_program.stages[prev_idx].is_set) {
        /* Use the runtime-derived planned duration of the stage that just
         * ended — NOT t_min. With rate-based ramps these can differ
         * substantially (t_min was computed against an assumed start temp).
         * If we don't have it tracked yet (e.g. very first stage change),
         * fall back to t_min so we don't underflow the estimate. */
        uint32_t planned = ctx->last_stage_planned_ms;
        if (planned == 0) {
            planned = (uint32_t)ctx->run_program.stages[prev_idx].t_min * 60U * 1000U;
        }
        uint32_t actual = (now_elapsed >= ctx->elapsed_at_stage_start_ms)
                        ? (now_elapsed - ctx->elapsed_at_stage_start_ms) : 0;

        if (planned > actual) {
            uint32_t saved = planned - actual;
            if (saved > ctx->heating_task_state.estimated_total_duration_ms) {
                ctx->heating_task_state.estimated_total_duration_ms = 0;
            } else {
                ctx->heating_task_state.estimated_total_duration_ms -= saved;
            }
            LOGGER_LOG_INFO(TAG, "Stage %d ended early: saved %lu ms (new total %lu ms)",
                            prev_idx,
                            (unsigned long)saved,
                            (unsigned long)ctx->heating_task_state.estimated_total_duration_ms);
        } else if (actual > planned) {
            /* Stage ran longer than budgeted (e.g. PID lagged behind the
             * ramp). Push the total estimate out by the overage so the
             * remaining-time display reflects reality. */
            uint32_t overrun = actual - planned;
            ctx->heating_task_state.estimated_total_duration_ms += overrun;
            LOGGER_LOG_INFO(TAG, "Stage %d ran long: +%lu ms (new total %lu ms)",
                            prev_idx,
                            (unsigned long)overrun,
                            (unsigned long)ctx->heating_task_state.estimated_total_duration_ms);
        }
    }

    ctx->last_profile_stage_index = tick->current_stage_index;
    ctx->elapsed_at_stage_start_ms = now_elapsed;
    /* Capture the new stage's runtime-derived planned duration so the next
     * stage-change can compute saved/overrun against the right baseline. */
    ctx->last_stage_planned_ms = tick->stage_planned_ms;
}

static void send_heater_command(heater_command_type_t type, float power_level)
{
    command_t command = {
        .target = COMMAND_TARGET_HEATER,
        .data.heater = {
            .type = type,
            .power_level = power_level
        }
    };
    post_heater_controller_command(&command);
}

static void kill_heater(void)
{
    send_heater_command(COMMAND_TYPE_HEATER_CLEAR, 0.0f);
    send_heater_command(COMMAND_TYPE_HEATER_SET_POWER, 0.0f);
}

static void set_control_inhibit(const bool inhibited)
{
    const esp_err_t err = heater_controller_set_control_inhibit(inhibited);
    if (err != ESP_OK)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to set heater control inhibit=%d: %s",
                         inhibited, esp_err_to_name(err));
    }
}

/**
 * @brief Pause the program on a recoverable run-time fault (stall / hold drift).
 *
 * Drops the heater and clears PID state (so a resume can't dump a stale
 * integral), then posts BOTH an error event — carrying stage/temp/duration
 * context so the HMI can tell the worker where and when it failed — and a
 * PROFILE_PAUSED event so the machine state leaves RUNNING. The run stays
 * paused for a human to inspect and decide whether to resume.
 */
static void enter_fault_pause(coordinator_ctx_t *ctx,
                              coordinator_error_code_t code,
                              const profile_tick_result_t *tick,
                              uint32_t fault_elapsed_ms)
{
    ctx->paused = true;
    ctx->heating_task_state.is_paused = true;
    set_control_inhibit(true);
    kill_heater();
    pid_controller_reset();

    coordinator_error_data_t err = {
        .error_code       = code,
        .esp_error_code   = ESP_FAIL,
        .temperature_c    = coordinator_get_current_temperature(ctx),
        .setpoint_c       = tick->setpoint,
        .stage_index      = active_stage_ordinal(ctx, tick->current_stage_index),
        .fault_elapsed_ms = fault_elapsed_ms,
    };
    post_coordinator_event(COORDINATOR_EVENT_ERROR_OCCURRED, &err, sizeof(err));

    /* Notify HMI / run indicator that we're now paused. Without this, only the
     * error event fires and the machine state never transitions out of RUNNING. */
    post_coordinator_event(COORDINATOR_EVENT_PROFILE_PAUSED, NULL, 0);
}

static void apply_pending_target_update(coordinator_ctx_t *ctx)
{
    if (!atomic_exchange(&ctx->target_update.pending, false)) {
        return;
    }

    int new_target    = ctx->target_update.target_t_c;
    int new_delta_x10 = ctx->target_update.delta_t_per_min_x10;
    float cur_temp    = coordinator_get_current_temperature(ctx);

    int abs_diff = new_target > (int)cur_temp
                 ? new_target - (int)cur_temp
                 : (int)cur_temp - new_target;
    uint32_t ramp_ms;
    if (new_delta_x10 > 0 && abs_diff > 0) {
        uint32_t ramp_min = ((uint32_t)abs_diff * 10U + (uint32_t)new_delta_x10 - 1U)
                          / (uint32_t)new_delta_x10;  /* ceiling div */
        if (ramp_min < 1) ramp_min = 1;
        ramp_ms = ramp_min * 60U * 1000U;
    } else {
        ramp_ms = 60U * 1000U;  /* 1 min minimum */
    }

    profile_update_stage_target((float)new_target, ramp_ms, cur_temp);

    ctx->heating_task_state.estimated_total_duration_ms =
        ctx->heating_task_state.current_time_elapsed_ms + ramp_ms;

    LOGGER_LOG_INFO(TAG, "Manual target applied: %d C, delta_x10=%d, ramp=%lu ms",
                    new_target, new_delta_x10, (unsigned long)ramp_ms);
}

static void handle_profile_completion(coordinator_ctx_t *ctx)
{
    LOGGER_LOG_INFO(TAG, "Profile complete (profile_tick): temp %.1f C",
                    coordinator_get_current_temperature(ctx));

    set_control_inhibit(true);
    kill_heater();

    ctx->heating_task_state.is_completed = true;
    post_coordinator_event(COORDINATOR_EVENT_PROFILE_COMPLETED, NULL, 0);
    ctx->running = false;
}

static void heater_controller_task(void* args)
{
    coordinator_ctx_t* ctx = (coordinator_ctx_t*)args;

    LOGGER_LOG_INFO(TAG, "Coordinator task started");

    uint32_t last_wake_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    profile_tick_result_t tick_result = {0};

    /* Stall detection state — tracks expected vs actual temp rise during HEATING */
    float  stall_start_temp     = coordinator_get_current_temperature(ctx);
    float  stall_start_setpoint = 0.0f;
    uint32_t stall_elapsed_ms   = 0;
    bool   stall_tracking       = false;

    /* Hold-band state — how long the chamber has been continuously outside
     * ±CONFIG_COORDINATOR_HOLD_BAND_C of the target during a HOLDING phase. */
    uint32_t hold_violation_ms  = 0;

    while (ctx->running)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t last_update_duration = 0;
        if (!ctx->paused)
        {
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            last_update_duration = current_time - last_wake_time;
            ctx->heating_task_state.current_time_elapsed_ms += last_update_duration;
            last_wake_time = current_time;
        }
        else
        {
            /* While paused, keep last_wake_time current so the first
               iteration after resume doesn't accumulate paused time. */
            last_wake_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            continue;   /* Skip PID computation while paused */
        }

        apply_pending_target_update(ctx);

        const profile_controller_error_t err = profile_tick(
            last_update_duration,
            coordinator_get_current_temperature(ctx),
            &tick_result);
        ctx->heating_task_state.target_temperature = tick_result.setpoint;

        LOGGER_LOG_INFO(TAG, "Elapsed: %lu ms, Stage: %d, Phase: %d, Setpoint: %.2f C",
                        (unsigned long)ctx->heating_task_state.current_time_elapsed_ms,
                        tick_result.current_stage_index,
                        (int)tick_result.phase,
                        tick_result.setpoint);

        if (err == PROFILE_CONTROLLER_ERROR_THRESHOLD_EXCEEDED)
        {
            LOGGER_LOG_ERROR(TAG, "EMERGENCY STOP: temperature overshoot threshold exceeded!");

            set_control_inhibit(true);
            kill_heater();

            /* Post critical furnace error */
            furnace_error_t furnace_err = {
                .severity = SEVERITY_CRITICAL,
                .source = SOURCE_COORDINATOR,
                .error_code = ERROR_CODE(0, 0, PROFILE_CONTROLLER_ERROR_THRESHOLD_EXCEEDED, 0)
            };
            event_manager_post_blocking(FURNACE_ERROR_EVENT,
                                        FURNACE_ERROR_EVENT_ID,
                                        &furnace_err, sizeof(furnace_err));

            ctx->heating_task_state.is_completed = true;
            post_coordinator_event(COORDINATOR_EVENT_PROFILE_COMPLETED, NULL, 0);
            ctx->running = false;
            break;
        }

        if (err != PROFILE_CONTROLLER_ERROR_NONE)
        {
            LOGGER_LOG_WARN(TAG, "profile_tick error: %d", err);
            continue;
        }

        if (tick_result.stage_changed) {
            LOGGER_LOG_INFO(TAG, "Stage changed → stage %d, phase %d",
                            tick_result.current_stage_index, (int)tick_result.phase);

            /* Shrink remaining time if the previous stage finished early. */
            update_estimate_on_stage_change(ctx, &tick_result);

            /* On cooling or cooldown entry: heater off, fan on */
            if (tick_result.phase == STAGE_PHASE_COOLING ||
                tick_result.phase == STAGE_PHASE_COOLDOWN) {
                kill_heater();
                ctx->heating_task_state.fan_on = true;
                /* TODO: Send fan-on command via GPIO or command handler when hardware interface is available */
                LOGGER_LOG_INFO(TAG, "%s: heater off, fan on",
                                tick_result.phase == STAGE_PHASE_COOLDOWN ? "Cooldown" : "Cooling stage");
            }
            /* Entering a dwell (typically from a HEATING ramp): hand the
             * integrator over to the steady hold power. During the ramp it wound
             * up to supply the *climbing* power; carried into the flat hold that
             * surplus keeps the elements firing past the target — the overshoot
             * we're chasing. With feedforward supplying the hold duty this
             * re-seeds the integrator to ~0, so the heater drops to hold power at
             * the corner instead of coasting up. (Feedforward off -> seeds to the
             * model estimate, which is the plain-reset value when uncalibrated.) */
            else if (tick_result.phase == STAGE_PHASE_HOLDING) {
                pid_controller_reset_for_setpoint(tick_result.setpoint);
                LOGGER_LOG_INFO(TAG, "Ramp->hold: integrator handed over at SP %.1f C",
                                tick_result.setpoint);
            }
        }

        float power_output = 0.0f;

        /* ── Stall detection during HEATING ─────────────────────────── */
        if (tick_result.phase == STAGE_PHASE_HEATING) {
            if (!stall_tracking || tick_result.stage_changed) {
                /* (Re)start tracking window */
                stall_start_temp     = coordinator_get_current_temperature(ctx);
                stall_start_setpoint = tick_result.setpoint;
                stall_elapsed_ms     = 0;
                stall_tracking       = true;
            } else {
                stall_elapsed_ms += last_update_duration;

                if (stall_elapsed_ms >= CONFIG_COORDINATOR_STALL_CHECK_MS) {
                    float expected_rise = tick_result.setpoint - stall_start_setpoint;
                    float actual_rise   = coordinator_get_current_temperature(ctx) - stall_start_temp;
                    float lag           = tick_result.setpoint - coordinator_get_current_temperature(ctx);
                    const float rate_fraction =
                        (float)CONFIG_COORDINATOR_STALL_RATE_FRACTION_PCT / 100.0f;

                    /* A genuine stall requires BOTH: we are meaningfully behind
                     * the setpoint (lag gate) AND we failed to achieve the
                     * programmed ramp rate. The lag gate is what prevents a false
                     * stall when we've overshot and the PID has correctly cut
                     * power — temp goes flat while the setpoint keeps climbing,
                     * but we're ahead, not stalled. */
                    if (lag > (float)CONFIG_COORDINATOR_STALL_MIN_LAG_C &&
                        expected_rise > 1.0f &&
                        actual_rise < expected_rise * rate_fraction) {
                        LOGGER_LOG_ERROR(TAG,
                            "STALL: %.1f C behind, expected +%.1f C in %lu ms, got +%.1f C",
                            lag, expected_rise,
                            (unsigned long)stall_elapsed_ms, actual_rise);

                        /* Pause (don't kill) so a worker can inspect & resume. */
                        enter_fault_pause(ctx, COORDINATOR_ERROR_STALL_DETECTED,
                                          &tick_result, stall_elapsed_ms);
                        stall_tracking = false;

                        /* Skip the rest of this tick — otherwise the PID block below
                         * runs and sends a SET_POWER that overrides kill_heater(). */
                        continue;
                    } else {
                        /* Window passed (or we're ahead of setpoint) — reset. */
                        stall_start_temp     = coordinator_get_current_temperature(ctx);
                        stall_start_setpoint = tick_result.setpoint;
                        stall_elapsed_ms     = 0;
                    }
                }
            }
        } else {
            stall_tracking = false;
        }

        /* ── Hold-band deviation during HOLDING ─────────────────────── */
        if (tick_result.phase == STAGE_PHASE_HOLDING) {
            float dev = coordinator_get_current_temperature(ctx) - tick_result.setpoint;
            if (dev < 0.0f) dev = -dev;

            if (dev > (float)CONFIG_COORDINATOR_HOLD_BAND_C) {
                hold_violation_ms += last_update_duration;
                if (hold_violation_ms >= CONFIG_COORDINATOR_HOLD_VIOLATION_MS) {
                    LOGGER_LOG_ERROR(TAG,
                        "HOLD DRIFT: %.1f C off target %.1f C for %lu ms",
                        dev, tick_result.setpoint,
                        (unsigned long)hold_violation_ms);

                    enter_fault_pause(ctx, COORDINATOR_ERROR_HOLD_DEVIATION,
                                      &tick_result, hold_violation_ms);
                    hold_violation_ms = 0;

                    /* Skip the rest of this tick (same reason as the stall path). */
                    continue;
                }
            } else {
                /* Back inside the band — reset the sustain timer. */
                hold_violation_ms = 0;
            }
        } else {
            hold_violation_ms = 0;
        }

        /* During cooling/cooldown the heater is off — skip PID computation */
        if (tick_result.phase != STAGE_PHASE_COOLDOWN &&
            tick_result.phase != STAGE_PHASE_COOLING) {
            /* Adaptive output ceiling is disabled by default
             * (CONFIG_PID_ADAPTIVE_OUTPUT_LIMIT_ENABLED=n). A vanilla PID with
             * appropriate Kd handles thermal-inertia overshoot on its own —
             * derivative-on-measurement subtracts power as temperature climbs,
             * before error reaches zero. Re-enable the line below only if you
             * find that a single gain set cannot cover the full temperature
             * range and you need to clamp output more aggressively at dwell. */
            /* pid_controller_set_adaptive_enabled(tick_result.phase == STAGE_PHASE_HOLDING); */

            const float dt_seconds = (float)last_update_duration / 1000.0f;
            power_output = pid_controller_compute(tick_result.setpoint,
                                                  coordinator_get_current_temperature(ctx),
                                                  dt_seconds);

            if (tick_result.stage_changed) {
                send_heater_command(COMMAND_TYPE_HEATER_CLEAR, power_output);
            }
            send_heater_command(COMMAND_TYPE_HEATER_SET_POWER, power_output);
        }

        post_status_update(ctx, &tick_result, power_output);

        if (tick_result.profile_complete) {
            handle_profile_completion(ctx);
            break;
        }

        event_manager_post_health(HEALTH_MONITOR_EVENT_HEARTBEAT, &coordinator_health_data);
    }

    LOGGER_LOG_INFO(TAG, "Temperature monitor task exiting");
    stop_heating_profile(ctx);
    vTaskDelete(NULL);
}

/**
 * @brief Estimate program duration from the actual ambient temperature.
 *
 * The profile controller honours the configured ramp rate per stage at
 * runtime (see advance_stage in temperature_profile_core.c), so the
 * pre-flight duration estimate must do the same — otherwise the HMI's
 * remaining-time display starts wrong and drifts further with every
 * stage. We walk the stages forward from the current ambient using:
 *
 *   - Rate-based duration ( |Δtemp| / rate ) for any stage that has a
 *     non-trivial temperature change AND a configured stage rate.
 *   - Stage's own rate preferred. If the stage has no rate (typical
 *     for cooling-direction stages, which the editor stores with
 *     delta_t_per_min_x10 = 0) and the temp is going DOWN, fall back
 *     to the program-level cooldown rate so the estimate isn't 0.
 *   - For pure dwell stages (target == previous target), use t_min
 *     directly as the hold duration.
 *
 * The duration estimate is "secondary to the ramp" — i.e. the rate is
 * the source of truth, and time displays are best-effort. This walk
 * may still be slightly off if real-world ramps lag or finish early,
 * which is fine — update_estimate_on_stage_change() trims the running
 * estimate as actual stages complete.
 */
static uint32_t calculate_program_duration_ms(const program_draft_t *prog,
                                               float current_temperature,
                                               int cooldown_rate_x10,
                                               uint32_t *out_stages_ms)
{
    uint32_t stages_ms = 0;
    float last_stage_temp = current_temperature;
    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        const program_stage_t *stage = &prog->stages[i];
        if (!stage->is_set) continue;

        const float target = (float)stage->target_t_c;
        float diff_c = target - last_stage_temp;
        const bool is_cooling = diff_c < 0.0f;
        if (is_cooling) diff_c = -diff_c;

        if (diff_c > 0.5f) {
            /* Real temperature change. Pick a rate. */
            int rate_x10 = stage->delta_t_per_min_x10;
            if (rate_x10 == 0 && is_cooling && cooldown_rate_x10 > 0) {
                rate_x10 = cooldown_rate_x10;
            }
            if (rate_x10 > 0) {
                /* duration_ms = |Δ| × 600_000 / rate_x10 */
                const float planned_ms_f = (diff_c * 600000.0f) / (float)rate_x10;
                stages_ms += (uint32_t)planned_ms_f;
            } else {
                /* No rate available — best we can do is the stored t_min. */
                stages_ms += (uint32_t)stage->t_min * 60U * 1000U;
            }
        } else {
            /* Pure dwell — t_min IS the hold time. */
            stages_ms += (uint32_t)stage->t_min * 60U * 1000U;
        }

        last_stage_temp = target;
    }

    uint32_t total_ms = stages_ms;
    if (last_stage_temp > 0.0f && cooldown_rate_x10 > 0) {
        float cooldown_min = (last_stage_temp * 10.0f) / (float)cooldown_rate_x10;
        if (cooldown_min < 1.0f) cooldown_min = 1.0f;
        total_ms += (uint32_t)(cooldown_min * 60.0f * 1000.0f);
    }

    *out_stages_ms = stages_ms;
    return total_ms;
}

static void init_heating_task_state(coordinator_ctx_t *ctx,
                                    uint32_t total_ms,
                                    uint32_t stages_ms)
{
    (void)total_ms;  /* Cooldown phase isn't counted in the displayed remaining time. */
    /* Clear the loop-gating pause flag. This is SEPARATE from
     * heating_task_state.is_paused (HMI reporting): ctx->paused is what the
     * control loop checks every tick, and it is only ever cleared here and in
     * resume_heating_profile. A stall (or manual pause) sets it true; without
     * clearing it on a fresh run, a stopped+restarted program would start a new
     * task that perpetually skips the PID/heater block — recoverable only by a
     * reboot (which zeroes the calloc'd context). */
    ctx->paused = false;
    ctx->heating_task_state.is_active = true;
    ctx->heating_task_state.is_paused = false;
    ctx->heating_task_state.is_completed = false;
    ctx->heating_task_state.current_time_elapsed_ms = 0;
    ctx->heating_task_state.estimated_total_duration_ms = stages_ms;
    ctx->heating_task_state.heating_stages_duration_ms = stages_ms;
    ctx->heating_task_state.current_temperature = coordinator_get_current_temperature(ctx);
    ctx->heating_task_state.heating_element_on = false;
    ctx->heating_task_state.fan_on = false;

    ctx->last_profile_stage_index  = -1;
    ctx->elapsed_at_stage_start_ms = 0;
    ctx->last_stage_planned_ms     = 0;
}

esp_err_t start_heating_profile(coordinator_ctx_t* ctx, const program_draft_t *program, int cooldown_rate_x10)
{
    if (ctx->task_handle != NULL && ctx->running)
    {
        return ESP_OK;
    }

    if (!program) {
        LOGGER_LOG_ERROR(TAG, "Program is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* A zero-initialized coordinator temperature is not a measurement. Do not
     * load a profile or queue heater commands until the processor has supplied
     * at least one accepted post-boot temperature event. Freshness during an
     * active run remains a separate F-001 phase. */
    if (!atomic_load_explicit(&ctx->has_valid_temperature, memory_order_acquire))
    {
        LOGGER_LOG_ERROR(TAG, "Refusing profile start: no valid temperature sample received");
        return ESP_ERR_INVALID_STATE;
    }

    if (atomic_load_explicit(&ctx->sensor_data_inhibited, memory_order_acquire))
    {
        LOGGER_LOG_ERROR(TAG, "Refusing profile start: sensor recovery is not complete");
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(&ctx->run_program, program, sizeof(ctx->run_program));
    ctx->has_program = true;
    const program_draft_t *prog = &ctx->run_program;

    uint32_t stages_ms = 0;
    uint32_t total_ms = calculate_program_duration_ms(prog, coordinator_get_current_temperature(ctx),
                                                     cooldown_rate_x10, &stages_ms);

    init_heating_task_state(ctx, total_ms, stages_ms);

    /* Clear any PID state left over from a previous run. pid_state is a static
     * (module-global) struct, so without this the integrator, previous
     * measurement and initialized flag carry over — a large leftover integral
     * from the prior run dumps near-max power on the first tick, overshooting
     * the new setpoint. */
    pid_controller_reset();

    const temp_profile_config_t temp_profile_config = {
        .program = prog,
        .initial_temperature = coordinator_get_current_temperature(ctx),
        .cooldown_rate_x10 = cooldown_rate_x10
    };

    const profile_controller_error_t err = load_heating_profile(temp_profile_config);

    if (err != PROFILE_CONTROLLER_ERROR_NONE)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to load heating profile '%s', error: %d",
                         prog->name,
                         err);
        return ESP_FAIL;
    }

    /* Set running BEFORE task creation — the new task checks ctx->running
     * in its while-loop condition and may be scheduled before we return. */
    ctx->running = true;

    CHECK_ERR_LOG_CALL_RET(xTaskCreate(
                               heater_controller_task,
                               coordinator_task_config.task_name,
                               coordinator_task_config.stack_size,
                               ctx,
                               coordinator_task_config.task_priority,
                               &ctx->task_handle) == pdPASS
                           ? ESP_OK
                           : ESP_FAIL,
                           ctx->task_handle = NULL; ctx->running = false,
                           "Failed to create coordinator task");

    /* Do not authorize the heater until the control task exists. If task
     * creation failed, no heater-start command has been submitted. */
    if (heater_controller_set_control_inhibit(false) != ESP_OK)
    {
        LOGGER_LOG_ERROR(TAG, "Refusing profile start: heater control inhibit could not be released");
        ctx->running = false;
        if (ctx->task_handle != NULL)
        {
            xTaskNotifyGive(ctx->task_handle);
        }
        return ESP_ERR_INVALID_STATE;
    }

    send_heater_command(COMMAND_TYPE_HEATER_CLEAR, 0.0f);
    send_heater_command(COMMAND_TYPE_HEATER_START, 0.0f);

    /* Start periodic PID tick timer */
    const esp_timer_create_args_t timer_args = {
        .callback = pid_tick_timer_cb,
        .arg = ctx,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "pid_tick"
    };
    esp_err_t timer_err = esp_timer_create(&timer_args, &ctx->pid_tick_timer);
    if (timer_err == ESP_OK) {
        esp_timer_start_periodic(ctx->pid_tick_timer,
                                 (uint64_t)CONFIG_COORDINATOR_PID_TICK_INTERVAL_MS * 1000ULL);
        LOGGER_LOG_INFO(TAG, "PID tick timer started (%d ms)", CONFIG_COORDINATOR_PID_TICK_INTERVAL_MS);
    } else {
        LOGGER_LOG_ERROR(TAG, "Failed to create PID tick timer: %s", esp_err_to_name(timer_err));
    }

    LOGGER_LOG_INFO(TAG, "Coordinator task initialized");

    event_manager_post_health(HEALTH_MONITOR_EVENT_REGISTER, &coordinator_health_data);

    return ESP_OK;
}

//TODO make sure it has control over heater and fan
//ask svetlio how long can a pause last and what temp difference is acceptable
esp_err_t pause_heating_profile(coordinator_ctx_t* ctx)
{
    if (!ctx->running)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ctx->paused = true;
    ctx->heating_task_state.is_paused = true;

    set_control_inhibit(true);

    /* Drop the SSR immediately and clear the target so the heater task
     * doesn't keep PWM-ing the last non-zero power level through the pause. */
    kill_heater();

    LOGGER_LOG_INFO(TAG, "Heating profile paused");

    return ESP_OK;
}

esp_err_t resume_heating_profile(coordinator_ctx_t* ctx)
{
    if (!ctx->running)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* Reset PID state so it starts fresh from the current temperature.
     * During pause the furnace may have cooled — a stale derivative term would
     * cause a large power spike on resume. Use the bumpless variant: it clears
     * the derivative history but seeds the integrator with the steady-state
     * hold power for the current setpoint, so the heater resumes near the right
     * duty instead of drooping while the integral winds back up from zero. */
    pid_controller_reset_for_setpoint(ctx->heating_task_state.target_temperature);

    /* Update the current temperature snapshot so the first PID tick
     * after resume uses the real measured value. */
    ctx->heating_task_state.current_temperature = coordinator_get_current_temperature(ctx);

    if (heater_controller_set_control_inhibit(false) != ESP_OK)
    {
        LOGGER_LOG_ERROR(TAG, "Heating profile resume rejected: heater control inhibit remains active");
        return ESP_ERR_INVALID_STATE;
    }

    ctx->paused = false;
    ctx->heating_task_state.is_paused = false;

    LOGGER_LOG_INFO(TAG, "Heating profile resumed (temp=%.1f C)",
                    coordinator_get_current_temperature(ctx));

    return ESP_OK;
}

esp_err_t get_heating_task_state(const coordinator_ctx_t* ctx)
{
    //TODO Post state event
    return ESP_FAIL;
}

esp_err_t get_current_heating_profile(const coordinator_ctx_t* ctx)
{
    //TODO Post current profile event
    return ESP_FAIL;
}

esp_err_t stop_heating_profile(coordinator_ctx_t *ctx)
{
    const TaskHandle_t task_handle = ctx->task_handle;
    const bool called_from_control_task = task_handle == xTaskGetCurrentTaskHandle();

    /* Always clean up timer and profile — the task may have self-exited
     * (profile complete / emergency stop) with ctx->running already false. */
    if (ctx->pid_tick_timer != NULL)
    {
        esp_timer_stop(ctx->pid_tick_timer);
        esp_timer_delete(ctx->pid_tick_timer);
        ctx->pid_tick_timer = NULL;
        LOGGER_LOG_INFO(TAG, "PID tick timer stopped");
    }

    if (ctx->running)
    {
        ctx->running = false;
        if (ctx->task_handle != NULL)
        {
            xTaskNotifyGive(ctx->task_handle);
        }
    }

    ctx->heating_task_state.is_paused = false;
    /* Also clear the loop-gating flag so the next start isn't born paused. */
    ctx->paused = false;

    set_control_inhibit(true);
    kill_heater();
    shutdown_profile_controller();

    send_heater_command(COMMAND_TYPE_HEATER_CLEAR, 0.0f);
    send_heater_command(COMMAND_TYPE_HEATER_STOP, 0.0f);

    if (task_handle != NULL)
    {
        if (called_from_control_task)
        {
            /* The worker must acknowledge before self-deleting, but cannot
             * wait on its own exit semaphore. */
            xSemaphoreGive(ctx->exit_semaphore);
        }
        else if (xSemaphoreTake(ctx->exit_semaphore, portMAX_DELAY) != pdTRUE)
        {
            return ESP_ERR_TIMEOUT;
        }
        ctx->task_handle = NULL;
    }

    LOGGER_LOG_INFO(TAG, "Coordinator task shutdown complete");

    return ESP_OK;
}

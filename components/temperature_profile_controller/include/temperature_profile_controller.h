#pragma once

#include "temperature_profile_types.h"

profile_controller_error_t load_heating_profile(const temp_profile_config_t config);

/**
 * @brief (Legacy) Get the planned/projected target temperature at a given time.
 *
 * Pure time-based linear interpolation through stages.  Used by the graph
 * builder to render the projected curve.  Does NOT account for real-world
 * temperature deviations — use profile_tick() for actual execution.
 */
profile_controller_error_t get_target_temperature_at_time(const uint32_t time_ms, float *temperature);

/**
 * @brief Advance the stateful profile controller by one tick.
 *
 * Must be called every PID interval.  Tracks stage progress, handles
 * the RAMP → HOLD → SETTLE → EXTEND state machine, and returns the
 * setpoint the PID should chase this tick.
 *
 * @param elapsed_since_last_ms  Wall time since the previous tick (ms).
 * @param current_temp           Actual measured furnace temperature (°C).
 * @param[out] result            Filled with setpoint, stage info, flags.
 * @return PROFILE_CONTROLLER_ERROR_NONE on success.
 */
profile_controller_error_t profile_tick(uint32_t elapsed_since_last_ms,
                                        float current_temp,
                                        profile_tick_result_t *result);

/**
 * @brief Reset the stateful tick state.
 *
 * Called when loading a new profile so that profile_tick() starts from
 * stage 0, phase RAMPING, with zero accumulated time.
 */
void profile_tick_reset(void);

/**
 * @brief Live-update the current stage target and re-ramp from now.
 *
 * Called from the profile task when a manual-mode target change is
 * pending.  Resets the ramp so it interpolates from 'current_temp'
 * to 'new_target' over 'new_planned_ms'.  Only affects the active
 * stage — has no effect during cooldown or completion.
 *
 * @param new_target      New target temperature (°C).
 * @param new_planned_ms  New stage duration (ms), computed from delta.
 * @param current_temp    Current measured temperature at this instant.
 * @return PROFILE_CONTROLLER_ERROR_NONE on success.
 */
profile_controller_error_t profile_update_stage_target(float new_target,
                                                       uint32_t new_planned_ms,
                                                       float current_temp);

profile_controller_error_t shutdown_profile_controller(void);

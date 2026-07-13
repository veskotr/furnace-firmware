#pragma once

#include <stdbool.h>

float pid_controller_compute(float setpoint, float measured_value, float dt);

void pid_controller_reset(void);

/**
 * @brief Reset the controller for a bumpless restart toward @p setpoint.
 *
 * Like pid_controller_reset() it clears the derivative history so a stale
 * previous measurement cannot produce a derivative spike on the next tick.
 * But instead of starting the integrator from zero — which makes the heater
 * droop while the integral winds back up — it seeds the integrator with the
 * steady-state hold power for @p setpoint from the feedforward model.
 *
 * If additive feedforward is enabled the integrator instead starts at zero,
 * because feedforward already supplies the base power every tick.
 *
 * Prefer this over pid_controller_reset() on resume-from-pause / restart.
 */
void pid_controller_reset_for_setpoint(float setpoint);

/**
 * @brief Enable / disable the adaptive (error-banded) output ceiling.
 *
 * When enabled, the upper output clamp shrinks as |error| shrinks — useful
 * for tight holding at setpoint without overshoot. When disabled, the PID
 * clamps to the static CONFIG_PID_OUTPUT_MAX regardless of error.
 *
 * Typical usage: enable during HOLDING (dwell at target), disable during
 * HEATING/COOLING ramps so the controller can supply enough power to track
 * the moving setpoint.
 *
 * Defaults to the value of CONFIG_PID_ADAPTIVE_OUTPUT_LIMIT_ENABLED.
 */
void pid_controller_set_adaptive_enabled(bool enabled);

/**
 * @brief Enable / disable the additive feedforward (steady-state power) term.
 *
 * When enabled, the open-loop base power from the feedforward model (calibrated
 * via the PID_FF_* Kconfig points) is added to the PID output every tick. This
 * keeps the integrator near zero and removes most steady-state droop and ramp
 * lag. Inert until the model is calibrated.
 *
 * Defaults to the value of CONFIG_PID_FEEDFORWARD_ENABLED.
 *
 * NOTE: do not combine with the adaptive output ceiling — near setpoint the
 * adaptive clamp can throttle the feedforward base power and reintroduce droop.
 */
void pid_controller_set_feedforward_enabled(bool enabled);


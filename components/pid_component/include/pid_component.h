#pragma once

#include <stdbool.h>

float pid_controller_compute(float setpoint, float measured_value, float dt);

void pid_controller_reset(void);

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


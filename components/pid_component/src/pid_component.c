#include "pid_component.h"
#include "logger_component.h"
#include "sdkconfig.h"
#include <stdbool.h>

static const char *TAG = "PID_COMPONENT";

typedef struct {
    float kp;
    float ki;
    float kd;
    float output_min;
    float output_max;
} pid_controller_params_t;

static const pid_controller_params_t pid_params = {
    /* Kp/Ki/Kd are stored in thousandths (range 0..1000) so menuconfig
     * stays integer-only. Output limits remain plain percent (0..100). */
    .kp = ((float) CONFIG_PID_KP / 1000.0f),
    .ki = ((float) CONFIG_PID_KI / 1000.0f),
    .kd = ((float) CONFIG_PID_KD / 1000.0f),
    .output_min = ((float) CONFIG_PID_OUTPUT_MIN / 100.0f),
    .output_max = ((float) CONFIG_PID_OUTPUT_MAX / 100.0f)
};

typedef struct {
    float integral;
    float previous_measurement;
    bool  initialized;
    bool  adaptive_enabled;
} pid_controller_state_t;

static pid_controller_state_t pid_state = {
    .integral = 0.0f,
    .previous_measurement = 0.0f,
    .initialized = false,
#if CONFIG_PID_ADAPTIVE_OUTPUT_LIMIT_ENABLED
    .adaptive_enabled = true
#else
    .adaptive_enabled = false
#endif
};

void pid_controller_set_adaptive_enabled(const bool enabled)
{
    if (pid_state.adaptive_enabled != enabled)
    {
        LOGGER_LOG_INFO(TAG, "PID adaptive ceiling: %s", enabled ? "ON" : "OFF");
    }
    pid_state.adaptive_enabled = enabled;
}

/* Compute the dynamic upper output clamp based on |error|.
 *
 * Furnace heating elements have significant thermal inertia: even after the
 * SSR turns off, the elements keep radiating stored heat into the chamber.
 * To avoid overshoot, we cap output more aggressively as we approach the
 * setpoint, so the elements never build up a large thermal reservoir right
 * before we arrive.
 *
 * Far from setpoint, the full configured output_max is allowed; near
 * setpoint, output is throttled to a small fraction (still non-zero so we
 * can hold against heat loss). */
static float compute_dynamic_output_max(const float abs_error)
{
#if CONFIG_PID_ADAPTIVE_OUTPUT_LIMIT_ENABLED
    if (!pid_state.adaptive_enabled)
    {
        return pid_params.output_max;
    }
    const float base = pid_params.output_max;
    if (abs_error >= (float)CONFIG_PID_ADAPTIVE_BAND_FAR_C)   return base;
    if (abs_error >= (float)CONFIG_PID_ADAPTIVE_BAND_MID_C)   return base * ((float)CONFIG_PID_ADAPTIVE_FACTOR_MID_PCT   / 100.0f);
    if (abs_error >= (float)CONFIG_PID_ADAPTIVE_BAND_NEAR_C)  return base * ((float)CONFIG_PID_ADAPTIVE_FACTOR_NEAR_PCT  / 100.0f);
    if (abs_error >= (float)CONFIG_PID_ADAPTIVE_BAND_CLOSE_C) return base * ((float)CONFIG_PID_ADAPTIVE_FACTOR_CLOSE_PCT / 100.0f);
    return base * ((float)CONFIG_PID_ADAPTIVE_FACTOR_HOLD_PCT / 100.0f);
#else
    (void)abs_error;
    return pid_params.output_max;
#endif
}

float pid_controller_compute(const float setpoint, const float measured_value, const float dt)
{
    if (dt <= 0.0f)
    {
        return 0.0f;
    }

    const float error = setpoint - measured_value;
    const float abs_error = error < 0.0f ? -error : error;

    /* Derivative on measurement (not on error): prevents a huge "derivative
     * kick" when the setpoint jumps, and gives predictive braking — output
     * backs off as the chamber temperature climbs, before error reaches 0.
     *
     * derivative = -d(measurement)/dt
     * On the first call after reset we don't have a valid previous sample,
     * so we treat the rate as 0. */
    float measurement_rate = 0.0f;
    if (pid_state.initialized)
    {
        measurement_rate = (measured_value - pid_state.previous_measurement) / dt;
    }
    const float derivative = -measurement_rate;

    /* Tentatively integrate; commit only if not saturated against the error. */
    const float tentative_integral = pid_state.integral + error * dt;

    const float p_term = pid_params.kp * error;
    const float i_term = pid_params.ki * tentative_integral;
    const float d_term = pid_params.kd * derivative;
    const float unclamped = p_term + i_term + d_term;

    /* Adaptive upper clamp shrinks as we approach setpoint. */
    const float dyn_output_max = compute_dynamic_output_max(abs_error);

    float output = unclamped;
    bool saturated_high = false;
    bool saturated_low  = false;
    if (output > dyn_output_max)
    {
        output = dyn_output_max;
        saturated_high = true;
    }
    else if (output < pid_params.output_min)
    {
        output = pid_params.output_min;
        saturated_low = true;
    }

    /* Anti-windup (conditional integration): only commit the integral update
     * if we are not saturated, or if the new error would pull the output
     * back out of saturation. With the adaptive ceiling this also stops the
     * integral from winding up against the *dynamic* limit near setpoint. */
    const bool error_pushes_further_into_high_sat = saturated_high && error > 0.0f;
    const bool error_pushes_further_into_low_sat  = saturated_low  && error < 0.0f;
    if (!error_pushes_further_into_high_sat && !error_pushes_further_into_low_sat)
    {
        pid_state.integral = tentative_integral;
    }

    pid_state.previous_measurement = measured_value;
    pid_state.initialized = true;

    LOGGER_LOG_INFO(TAG,
                     "PID - SP: %.2f, PV: %.2f, err: %.2f, dt: %.3fs, dyn_max: %.2f, P: %.3f, I: %.3f, D: %.3f, Out: %.3f",
                     setpoint, measured_value, error, dt, dyn_output_max, p_term, i_term, d_term, output);

    return output;
}

void pid_controller_reset(void)
{
    pid_state.integral = 0.0f;
    pid_state.previous_measurement = 0.0f;
    pid_state.initialized = false;
    LOGGER_LOG_INFO(TAG, "PID controller reset");
}

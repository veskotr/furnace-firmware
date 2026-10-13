#include "pid_component.h"
#include "logger_component.h"
#include "sdkconfig.h"
#include <stdbool.h>
#include <math.h>

/* Fallbacks for the feedforward calibration points, in case this component is
 * built against an sdkconfig that predates them (e.g. before the first
 * menuconfig regen). The inert defaults (0 % at both points) make the model
 * return 0, i.e. behave exactly like a plain PID. */
#ifndef CONFIG_PID_FEEDFORWARD_ENABLED
#define CONFIG_PID_FEEDFORWARD_ENABLED 0
#endif
#ifndef CONFIG_PID_FF_T_LOW_C
#define CONFIG_PID_FF_T_LOW_C 100
#endif
#ifndef CONFIG_PID_FF_PCT_LOW
#define CONFIG_PID_FF_PCT_LOW 0
#endif
#ifndef CONFIG_PID_FF_T_HIGH_C
#define CONFIG_PID_FF_T_HIGH_C 200
#endif
#ifndef CONFIG_PID_FF_PCT_HIGH
#define CONFIG_PID_FF_PCT_HIGH 0
#endif

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

/* Ki below this is treated as "no integral action" — used to avoid a
 * divide-by-zero when back-solving the integrator for a bumpless restart. */
#define PID_KI_EPSILON 1e-9f

typedef struct {
    float integral;
    float previous_measurement;
    bool  initialized;
    bool  adaptive_enabled;
    bool  feedforward_enabled;
} pid_controller_state_t;

static pid_controller_state_t pid_state = {
    .integral = 0.0f,
    .previous_measurement = 0.0f,
    .initialized = false,
#if CONFIG_PID_ADAPTIVE_OUTPUT_LIMIT_ENABLED
    .adaptive_enabled = true,
#else
    .adaptive_enabled = false,
#endif
#if CONFIG_PID_FEEDFORWARD_ENABLED
    .feedforward_enabled = true,
#else
    .feedforward_enabled = false,
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

void pid_controller_set_feedforward_enabled(const bool enabled)
{
    if (pid_state.feedforward_enabled != enabled)
    {
        LOGGER_LOG_INFO(TAG, "PID feedforward: %s", enabled ? "ON" : "OFF");
    }
    pid_state.feedforward_enabled = enabled;
}

/* Feedforward / steady-state power model.
 *
 * Returns the open-loop duty (0..1) the heater needs JUST to hold `setpoint`
 * against heat loss, before the closed-loop P/I/D terms correct the residual.
 * Furnace loss climbs steeply with temperature, so the base power is modelled
 * as a straight line between two field-measured calibration points, clamped
 * flat outside that range:
 *
 *     (PID_FF_T_LOW_C  -> PID_FF_PCT_LOW)
 *     (PID_FF_T_HIGH_C -> PID_FF_PCT_HIGH)
 *
 * With the inert defaults (both percentages 0) this returns 0, so the
 * controller behaves exactly like a plain PID until the points are calibrated.
 * Used both as the additive feedforward term (when enabled) and as the
 * steady-state estimate for the integral seed on a bumpless restart. */
static float pid_feedforward_model(const float setpoint)
{
    const float t_lo = (float)CONFIG_PID_FF_T_LOW_C;
    const float t_hi = (float)CONFIG_PID_FF_T_HIGH_C;
    const float p_lo = (float)CONFIG_PID_FF_PCT_LOW  / 100.0f;
    const float p_hi = (float)CONFIG_PID_FF_PCT_HIGH / 100.0f;

    float ff;
    if (t_hi <= t_lo)
    {
        ff = p_lo;   /* degenerate calibration — fall back to the low point */
    }
    else
    {
        float frac = (setpoint - t_lo) / (t_hi - t_lo);
        if (frac < 0.0f) frac = 0.0f;   /* clamp flat below T_LOW */
        if (frac > 1.0f) frac = 1.0f;   /* clamp flat above T_HIGH */
        ff = p_lo + (p_hi - p_lo) * frac;
    }

    if (ff < 0.0f) ff = 0.0f;
    if (ff > pid_params.output_max) ff = pid_params.output_max;
    return ff;
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
    if (!isfinite(setpoint) || !isfinite(measured_value) || !isfinite(dt) || dt <= 0.0f ||
        !isfinite(pid_state.integral) ||
        (pid_state.initialized && !isfinite(pid_state.previous_measurement)))
    {
        LOGGER_LOG_ERROR(TAG, "Rejected non-finite PID input or state; resetting controller");
        pid_controller_reset();
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

    /* Feedforward: open-loop base power to HOLD this setpoint against heat
     * loss. Lets the integrator stay near zero, removing most steady-state
     * droop and ramp lag. Inert (0) until calibrated — see Kconfig PID_FF_*. */
    const float ff_term = pid_state.feedforward_enabled
                        ? pid_feedforward_model(setpoint)
                        : 0.0f;

    const float p_term = pid_params.kp * error;
    const float i_term = pid_params.ki * tentative_integral;
    const float d_term = pid_params.kd * derivative;
    const float unclamped = p_term + i_term + d_term + ff_term;

    if (!isfinite(unclamped))
    {
        LOGGER_LOG_ERROR(TAG, "Rejected non-finite PID output; resetting controller");
        pid_controller_reset();
        return 0.0f;
    }

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
                     "PID - SP: %.2f, PV: %.2f, err: %.2f, dt: %.3fs, dyn_max: %.2f, P: %.3f, I: %.3f, D: %.3f, FF: %.3f, Out: %.3f",
                     setpoint, measured_value, error, dt, dyn_output_max, p_term, i_term, d_term, ff_term, output);

    return output;
}

void pid_controller_reset(void)
{
    pid_state.integral = 0.0f;
    pid_state.previous_measurement = 0.0f;
    pid_state.initialized = false;
    LOGGER_LOG_INFO(TAG, "PID controller reset");
}

void pid_controller_reset_for_setpoint(const float setpoint)
{
    if (!isfinite(setpoint))
    {
        LOGGER_LOG_ERROR(TAG, "Rejected non-finite PID reset setpoint; resetting controller");
        pid_controller_reset();
        return;
    }

    /* Always clear the derivative history: a stale previous_measurement across
     * a pause would otherwise produce a large (and bogus) derivative spike on
     * the first tick after resume. */
    pid_state.previous_measurement = 0.0f;
    pid_state.initialized = false;

    /* Bumpless restart: seed the integrator with the steady-state hold power so
     * the heater resumes near the right duty instead of dropping to zero and
     * slowly winding back up (which shows up as a temperature droop on resume).
     *
     *  - If additive feedforward is ENABLED it already supplies that base power
     *    on every tick, so the integrator must start at zero to avoid double-
     *    counting.
     *  - If feedforward is DISABLED we use its model purely as the estimate for
     *    the integral seed (integral = power / Ki, so the I-term contributes
     *    that power at zero error). With an uncalibrated/inert model this is 0,
     *    i.e. identical to a plain reset.
     *
     * The P and D terms act normally on top, so a furnace that cooled during
     * the pause still gets the extra reheat power from the proportional term. */
    float seed_integral = 0.0f;
    if (!pid_state.feedforward_enabled && pid_params.ki > PID_KI_EPSILON)
    {
        const float steady_state = pid_feedforward_model(setpoint);
        seed_integral = steady_state / pid_params.ki;
    }
    pid_state.integral = seed_integral;

    LOGGER_LOG_INFO(TAG,
                    "PID bumpless reset @ SP %.1f C: integral seeded to %.4f (I-term=%.3f)",
                    setpoint, seed_integral, pid_params.ki * seed_integral);
}

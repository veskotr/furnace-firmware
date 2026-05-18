#include "pid_component.h"
#include "logger_component.h"
#include "sdkconfig.h"
#include <stdbool.h>

static const char *TAG = "PID_COMPONENT";

// PID controller parameters
typedef struct {
    float kp;
    float ki;
    float kd;
    float output_min;
    float output_max;
} pid_controller_params_t;

static const pid_controller_params_t pid_params = {
    .kp = ((float) CONFIG_PID_KP / 100.0f),
    .ki = ((float) CONFIG_PID_KI / 100.0f),
    .kd = ((float) CONFIG_PID_KD / 100.0f),
    .output_min = ((float) CONFIG_PID_OUTPUT_MIN / 100.0f),
    .output_max = ((float) CONFIG_PID_OUTPUT_MAX / 100.0f)
};

typedef struct {
    float integral;
    float previous_error;
} pid_controller_state_t;

static pid_controller_state_t pid_state = {
    .integral = 0.0f,
    .previous_error = 0.0f
};

float pid_controller_compute(const float setpoint, const float measured_value, const float dt)
{
    if (dt <= 0.0f)
    {
        return 0.0f;
    }

    const float error = setpoint - measured_value;

    /* Tentatively integrate, then compute the unclamped output. */
    const float tentative_integral = pid_state.integral + error * dt;
    const float derivative = (error - pid_state.previous_error) / dt;

    const float p_term = pid_params.kp * error;
    const float i_term = pid_params.ki * tentative_integral;
    const float d_term = pid_params.kd * derivative;
    const float unclamped = p_term + i_term + d_term;

    /* Clamp output to min/max. */
    float output = unclamped;
    bool saturated_high = false;
    bool saturated_low = false;
    if (output > pid_params.output_max)
    {
        output = pid_params.output_max;
        saturated_high = true;
    }
    else if (output < pid_params.output_min)
    {
        output = pid_params.output_min;
        saturated_low = true;
    }

    /* Anti-windup (conditional integration): only commit the integral update
     * if we are not saturated, or if the new error would pull the output
     * back out of saturation. This stops the integral from "storing up"
     * demand while the heater is already at max. */
    const bool error_pushes_further_into_high_sat = saturated_high && error > 0.0f;
    const bool error_pushes_further_into_low_sat  = saturated_low  && error < 0.0f;
    if (!error_pushes_further_into_high_sat && !error_pushes_further_into_low_sat)
    {
        pid_state.integral = tentative_integral;
    }

    pid_state.previous_error = error;

    LOGGER_LOG_DEBUG(TAG, "PID Compute - Setpoint: %.2f, Measured: %.2f, dt: %.3f s, P: %.3f, I: %.3f, D: %.3f, Output: %.3f",
                     setpoint, measured_value, dt, p_term, i_term, d_term, output);

    return output;
}

void pid_controller_reset(void)
{
    pid_state.integral = 0.0f;
    pid_state.previous_error = 0.0f;
    LOGGER_LOG_INFO(TAG, "PID controller reset");
}


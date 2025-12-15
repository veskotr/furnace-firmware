#include "pid_component.h"
#include "logger_component.h"
#include "sdkconfig.h"

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

float pid_controller_compute(float setpoint, float measured_value, float dt)
{
    float error = setpoint - measured_value;
    pid_state.integral += error * dt;
    float derivative = (error - pid_state.previous_error) / dt;

    float output = (pid_params.kp * error) + (pid_params.ki * pid_state.integral) + (pid_params.kd * derivative);

    // Clamp output to min/max
    if (output > pid_params.output_max)
    {
        output = pid_params.output_max;
    }
    else if (output < pid_params.output_min)
    {
        output = pid_params.output_min;
    }

    pid_state.previous_error = error;

    LOGGER_LOG_DEBUG(TAG, "PID Compute - Setpoint: %.2f, Measured: %.2f, Output: %.2f",
                     setpoint, measured_value, output);

    return output;
}

void pid_controller_reset(void)
{
    pid_state.integral = 0.0f;
    pid_state.previous_error = 0.0f;
    LOGGER_LOG_INFO(TAG, "PID controller reset");
}


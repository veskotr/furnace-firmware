#include "pid_component.h"
#include "logger_component.h"
#include "sdkconfig.h"

static const char *TAG = "PID_COMPONENT";

// PID controller parameters
static float kp = ((float) CONFIG_PID_KP / 100.0f); 
static float ki = ((float) CONFIG_PID_KI / 100.0f);
static float kd = ((float) CONFIG_PID_KD / 100.0f);
static float output_min =  ((float) CONFIG_PID_OUTPUT_MIN / 100.0f);
static float output_max = ((float) CONFIG_PID_OUTPUT_MAX / 100.0f);
static float integral = 0.0f;
static float previous_error = 0.0f;

float pid_controller_compute(float setpoint, float measured_value, float dt)
{
    float error = setpoint - measured_value;
    integral += error * dt;
    float derivative = (error - previous_error) / dt;

    float output = (kp * error) + (ki * integral) + (kd * derivative);

    // Clamp output to min/max
    if (output > output_max)
    {
        output = output_max;
    }
    else if (output < output_min)
    {
        output = output_min;
    }

    previous_error = error;

    LOGGER_LOG_DEBUG(TAG, "PID Compute - Setpoint: %.2f, Measured: %.2f, Output: %.2f",
                     setpoint, measured_value, output);

    return output;
}

void pid_controller_reset(void)
{
    integral = 0.0f;
    previous_error = 0.0f;
    LOGGER_LOG_INFO(TAG, "PID controller reset");
}


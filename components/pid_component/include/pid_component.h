#pragma once

float pid_controller_compute(float setpoint, float measured_value, float dt);

void pid_controller_reset(void);


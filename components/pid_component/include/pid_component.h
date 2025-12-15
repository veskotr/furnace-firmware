#ifndef PID_COMPONENT_H
#define PID_COMPONENT_H

float pid_controller_compute(float setpoint, float measured_value, float dt);

void pid_controller_reset(void);

#endif // PID_COMPONENT_H
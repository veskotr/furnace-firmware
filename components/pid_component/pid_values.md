Here are saved tested pid values for different devices.

15KW furnace pid values:

Kp: 100 
Ki: 1
Kd: 30 (was 1; bumped for arrival braking)
Max Output: 100%
PID interval: 1000ms
Heater window: 1000ms /5000ms?
Feedforward low: 7
Feedforward high: 12

Feedforward: ENABLED, additive. FIELD-VALIDATED - keep it ON (it carries the hold
  duty so the integrator stays small). Low point T_LOW=80C, PCT_LOW=8 (steady hold
  Out settled ~0.08 at 80C; 10% was ~2% high and made the integral swing negative
  -> a post-transition dip). High point T_HIGH=150 / PCT_HIGH=10 still UNCALIBRATED:
  hold duty RISES with temp, so read steady Out at a 120-150C hold and set PCT_HIGH
  higher (likely 12-18%). FF is per-furnace -- the weaker unit may want a lower PCT_LOW.
Ramp->hold integral handover: ENABLED (reset_for_setpoint at the corner; with FF on
  it seeds the integrator to ~0). Field result: ramp tracks ~0.8C under SP, the
  transition peaks ~81.5-82C then briefly dips (~77.9C on the weaker unit) and
  settles within ~+-0.7C.
Open: ramp-tracking lag (~0.8C steady, ~4C spike at a mid-program ramp start). Clean
  fix is velocity (rate) feedforward; Kv ~6 estimated from a log (I-term ~0.285 at a
  ~0.05 C/s ramp). Higher Kp also cuts lag but worsens the transition overshoot.

25KW furnace pid values:

Kp: 95
Ki: 1
Kd: 30
Max Output: 100%
PID interval: 1000ms
Heater window: 5000ms
Feedforward low: 7
Feedforward high: 12
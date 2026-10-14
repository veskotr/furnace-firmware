# 04 — Control algorithms: the final legacy math

These are the formulas the legacy firmware ran at hand-over, with the values of the field-used build. They are reference for the new design, not a specification to copy. Where the final behavior is a defect, it is marked, and the requirement for the new project is in [06-REQUIREMENTS-BASELINE.md](06-REQUIREMENTS-BASELINE.md). Legacy identifiers in backticks are names in the old code, kept only for orientation.

## 1. Units and scaling

| Quantity | Storage | Runtime unit | Where converted |
| --- | --- | --- | --- |
| Stage target | `int` °C | `float` °C | `advance_stage` |
| Stage rate | `int` x10 (15 = 1.5 °C/min), signed | °C/min | `advance_stage`: `diff × 600000 / rate_x10` gives ms |
| Stage time | `int` minutes | ms (`× 60000`) | `advance_stage`, `calculate_program_duration_ms` |
| Handover tolerance | Kconfig x10 (5 = 0.5 °C) | °C | `profile_tick` |
| Shared tolerance | Kconfig whole °C (2) | °C | phase detect, cooling advance, validation |
| PID gains | Kconfig thousandths (Kp=100 → 0.100) | duty per °C, duty per (°C·s), duty per (°C/s) | `pid_params` |
| PID output limits | Kconfig percent | fraction 0–1 | `pid_params` |
| Feedforward points | Kconfig °C and percent | fraction | `pid_feedforward_model` |
| Elapsed time | `uint32_t` ms from `xTaskGetTickCount() × portTICK_PERIOD_MS` (10 ms resolution) | ms | control task |
| Sensor value | IEEE-754 float from two Modbus registers | °C | `ms9024_read_float` |

## 2. Stage state machine

```
            first tick / advance_stage(T)
                       |
         +-------------+--------------+
         v             v              v
     HEATING        HOLDING        COOLING            (phase from measured T at stage start)
 T >= tgt-0.5    elapsed >= t_min   T <= tgt+2
         \             |              /
          +------> advance_stage(T) <-+   (no more stages)
                       |
                       v
                   COOLDOWN  --T < 40 C-->  COMPLETE
```

Phase detection at stage entry, with T the measured temperature and tol = 2 °C:

- HEATING if `target > T + tol`
- COOLING if `target < T − tol`
- otherwise HOLDING

Planned stage duration:

- HEATING with rate > 0: `planned_ms = (|target − T| + min(B, |target − T|)) × 600000 / rate_x10`, where B is the ease band (6 °C).
- Every other case (HOLDING, COOLING, HEATING with rate 0): `planned_ms = t_min × 60000`.

On the tick a stage advances, the reported setpoint equals the new stage's start temperature (the measured T). The new ramp starts on the following tick.

## 3. Setpoint shaping: linear ramp plus quadratic ease-out (`ramp_ease_position`)

With span `S = target − start` (S > 0), band `b = min(B, S)`, and elapsed fraction `f = min(1, elapsed / planned)`:

```
total = S + b
t_e   = (S − b) / total          # time fraction where easing starts
w     = 2b / total               # time fraction of the ease segment
if f <= t_e:   q = f · total / S
else:          u = (f − t_e) / w
               q = (S − b)/S + (b/S) · (2u − u²)
setpoint = start + S · q
```

Properties: C¹-continuous at the seam; monotonic; the rate starts at the programmed rate and falls linearly to 0; it never exceeds the programmed rate. The ease segment takes twice its linear time, which is why one band duration is added to `planned_ms`. With B = 0 the ramp is pure linear.

Lesson (legacy F-048): an earlier cubic easing curve briefly *exceeded* the ramp rate inside the band and made the arrival overshoot worse. The rate bound must be a tested property (rule R10).

Known divergences:

- The HMI graph projection (`get_target_temperature_at_time`, `heating_program_graph.c`) is linear and `t_min`-based, so it does not match the runtime trajectory.
- The manual live-update path sets `planned_ms = |Δ| / rate` **without** the ease extension, so its effective linear rate is `(S + b) / S` times the requested rate (F-065).

## 4. PID

```
e      = SP − PV
rate   = initialized ? (PV − PV_prev) / dt : 0       # derivative on measurement
I_new  = I + e · dt                                  # tentative integral, °C·s
FF     = ff_enabled ? model(SP) : 0
u_raw  = Kp·e + Ki·I_new + Kd·(−rate) + FF
u      = clamp(u_raw, out_min, out_max_dyn)          # out_max_dyn = out_max unless adaptive ceiling on
commit I = I_new unless (u saturated high and e > 0) or (u saturated low and e < 0)
PV_prev = PV; initialized = true
```

- Non-finite SP/PV/dt, `dt ≤ 0`, non-finite state, or a non-finite `u_raw` resets the controller and returns 0.
- Every tick logs P, I, D, FF and output at INFO; this was the field-tuning data source.
- There is one file-static instance (singleton).

Reset variants:

- `pid_controller_reset()` sets `I = 0` and clears the derivative history. It runs at start, on stop, and on fault pause.
- `pid_controller_reset_for_setpoint(SP)` clears the derivative history. If FF is on, `I = 0`; otherwise `I = model(SP) / Ki`, so the I-term equals the steady-state estimate. It runs at the ramp→hold handover and on resume.

Feedforward model, with points (T_lo, P_lo) and (T_hi, P_hi):

```
f  = clamp((SP − T_lo) / (T_hi − T_lo), 0, 1)
FF = clamp(P_lo + (P_hi − P_lo) · f, 0, out_max)     # degenerate T_hi <= T_lo gives P_lo
```

The adaptive output ceiling (`PID_ADAPTIVE_*`) is compiled out by default and its call site is commented out in the coordinator. Do not combine it with FF.

## 5. Actuation

```
loop every window W (5000 ms):
    p = target_power (mutex)                     # latest SET_POWER, 0..1
    on = p · W ; off = W − on
    if on > 0 and not inhibited:  SSR=1 ; wait(on)
    if off > 0:                   SSR=0 ; wait(off)
```

- The PID publishes every 1 s, but the actuator samples once per 5 s window. The effective actuator update rate is 0.2 Hz with up to 5 s added latency (F-078).
- `HEATER_CLEAR` and every inhibit drive the SSR low immediately. The PWM loop is **not** woken, so a new positive power after a clear waits for the current window to end.
- At p = 1.0 no off phase occurs, and the SSR is re-asserted each window.

## 6. Guards

| Guard | Phase(s) | Condition | Action |
| --- | --- | --- | --- |
| Overshoot | HEATING, HOLDING, **COOLING** | `T > setpoint + 20` (field) | Control inhibit, `FURNACE_ERROR_EVENT`, `PROFILE_COMPLETED`, task exits. Defect in COOLING: F-057; reporting: F-058 |
| Stall | HEATING | Every `STALL_CHECK_MS` (300 s field): `lag = SP − T > 5` **and** `SP − SP_start > 1.0` (hard-coded) **and** `T − T_start < 0.5 × (SP − SP_start)` | Fault pause `COORDINATOR_ERROR_STALL_DETECTED`; otherwise restart the window |
| Hold deviation | HOLDING | `\|T − SP\| > 4` accumulated continuously; reset on re-entry | After 300 s, fault pause `COORDINATOR_ERROR_HOLD_DEVIATION` |
| Sensor freshness | all | Aggregate invalid, or no accepted aggregate for 5 s | Direct sensor-data inhibit plus pause |

Paused time does not advance the stall or hold timers or the stage time, because the control loop skips while paused.

## 7. Field-tuning note (recorded, not carried over)

Per D-15, the field tuning is **recorded here as a note**. It is not carried over as configuration. The new controller has a different MCU, possibly different sensors, and possibly a different actuation scheme (T-14), so its loop must be re-tuned and validated. These numbers are a starting point and a regression reference for the plant model, nothing more. How the new controller stores PID values is deferred (T-03, T-16).

Field build values at hand-over (15 kW unit; field-reported):

| Parameter | Value |
| --- | --- |
| Kp / Ki / Kd | 0.100 / 0.001 / 0.030 (configured as 100 / 1 / 30 thousandths) |
| Output limits | 0 … 95 % |
| Feedforward | on; 80 °C → 8 %, 150 °C → 12 % (high point **uncalibrated**) |
| PID period / SSR window | 1 s / 5 s |
| Ease band / handover tolerance | 6 °C / 0.5 °C |
| Overshoot threshold | 20 °C (but see F-057: must not end cooling, D-06) |
| Stall window / min lag / rate fraction | 300 s / 5 °C / 50 % |
| Hold band / sustain | 4 °C / 300 s |
| Fan thresholds (idle) | 43 / 41 °C |

The 25 kW unit was recorded with Kp 95, Ki 1, Kd 30, out max 100 %, window 5 s, FF 7 / 12 %. Its full configuration was never captured.

What the tuning taught (field-reported, 2026-05/06):

- **Integral windup and element thermal lag dominate the ramp→hold overshoot.** The heater kept firing to about +3 °C, and the elements coast 4–5 °C after heater-off from stored heat. Setpoint shaping alone did not remove it.
- **Additive feedforward was the decisive fix.** It carries the steady-state hold duty (~8 % at 80 °C on the 15 kW unit), so the integrator stays near zero. Combined with an integrator handover at the ramp→hold corner, the arrival peak dropped to about +1.5–2 °C, followed by a short dip, then settling within ±0.7 °C. The team decided to **keep FF on**.
- **The handover must happen close to the target.** Handing over 2 °C early let the last 2 °C climb under proportional push and coast past; a dedicated 0.5 °C handover tolerance fixed that.
- **Kd is nearly useless at these ramp rates.** The chamber rises only ~0.05 °C/s, so the D-term contributed ±0.1–0.5 %.
- **Steady ramp lag is ~0.8 °C**, with a ~4 °C spike when a ramp starts mid-program. A higher Kp reduces the lag but worsens the arrival. The proposed remedy (not built) was velocity feedforward `Kv · dSP/dt` during HEATING, fading to zero at the corner (Kv ≈ 6 estimated).
- **Hold duty rises with temperature.** Measure the FF high point at a 120–150 °C hold before trusting higher holds.
- **Guards needed loosening.** Stall and hold windows of 300 s avoided nuisance pauses. The stall test (lag AND rate fraction) was judged weak; the planned replacement was a sustained under-temperature lag guard symmetric with the hold guard.
- **Log the P, I, D and FF terms each tick.** Those traces made tuning possible. In the new project, keep them as rate-limited structured trace records, not text logs on the control path.

## 8. What the new project should pin down first

Write these as pure-logic tests before any RTOS code (the full backlog is in [09](09-ZEPHYR-MAPPING-AND-TEST-BACKLOG.md)):

1. Ramp planner: endpoints, monotonicity, rate ≤ programmed rate on every entry path, band = 0, band ≥ span.
2. Stage machine with **explicit** stage kinds (R9), including warm start and "already at target".
3. PID: P/I/D terms, anti-windup in both directions, derivative on measurement, both reset variants, non-finite handling.
4. FF model: below, between and above the points; degenerate points; clamp.
5. Guards: cooling never ends the run (D-06, T-04); absolute limit (T-10); hold deviation; replay of a recorded field trace without nuisance trips.
6. Duration estimate and its correction at stage changes.
7. SSR window planning at the chosen tick resolution (T-14).

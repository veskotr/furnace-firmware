# 03 — Control algorithms, exact math and tuning history

All formulas come from current source at `d1b0fda`. The symbols are the field-build values unless stated otherwise.

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

## 2. Stage state machine (`temperature_profile_core.c:profile_tick`)

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

History (F-048): the previous cubic `q = f0 + w(u + u² − u³)` peaked at 4/3 of the ramp rate inside the band. That accelerated the setpoint right before the corner and matched the observed ramp→hold overshoot.

Known divergences:

- The HMI graph projection (`get_target_temperature_at_time`, `heating_program_graph.c`) is linear and `t_min`-based, so it does not match the runtime trajectory.
- The manual live-update path sets `planned_ms = |Δ| / rate` **without** the ease extension, so its effective linear rate is `(S + b) / S` times the requested rate (F-065).

## 4. PID (`pid_component.c:pid_controller_compute`)

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

## 5. Actuation (`heater_controller_task.c`)

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

## 6. Guards (`coordinator_component_heater_controller.c:heater_controller_task`, `profile_tick`)

| Guard | Phase(s) | Condition | Action |
| --- | --- | --- | --- |
| Overshoot | HEATING, HOLDING, **COOLING** | `T > setpoint + 20` (field) | Control inhibit, `FURNACE_ERROR_EVENT`, `PROFILE_COMPLETED`, task exits. Defect in COOLING: F-057; reporting: F-058 |
| Stall | HEATING | Every `STALL_CHECK_MS` (300 s field): `lag = SP − T > 5` **and** `SP − SP_start > 1.0` (hard-coded) **and** `T − T_start < 0.5 × (SP − SP_start)` | Fault pause `COORDINATOR_ERROR_STALL_DETECTED`; otherwise restart the window |
| Hold deviation | HOLDING | `\|T − SP\| > 4` accumulated continuously; reset on re-entry | After 300 s, fault pause `COORDINATOR_ERROR_HOLD_DEVIATION` |
| Sensor freshness | all | Aggregate invalid, or no accepted aggregate for 5 s | Direct sensor-data inhibit plus pause |

Paused time does not advance the stall or hold timers or the stage time, because the control loop skips while paused.

## 7. Field-tuning history (field-reported, 2026-05 to 2026-06, `field-tests` branch)

Sources: commit messages in `196d2ff`, `components/pid_component/pid_values.md`, and team notes.

| Round | Observation | Change | Result |
| --- | --- | --- | --- |
| 0 | Plain PI in practice (Kd = 1 → 0.001), ramp and hold tracked within ~1 °C, ramp→hold overshoot ~4 °C settling in 1–2 min | — | Baseline |
| 1 | Integral windup plus a sharp corner | Kd 1 → 30; added soft-landing ease band 6 °C (then cubic) | Overshoot **not** reduced |
| 2 | 15 kW unit at 80 °C hold needs ~10 % duty; heater kept firing to ~83 °C; 4–5 °C free coast after heater off (element stored heat) | Enabled additive FF (80 °C → 10 %); integral handover at ramp→hold (`reset_for_setpoint`) | — |
| 3 | Peaks ~81.5–82 °C, brief dip to ~77.9 °C, then ±0.7 °C; steady hold output ≈ 0.08 with I ≈ −0.045 | FF low point trimmed 10 → 8 % | **Field-validated; keep FF on** |
| 4 | Residual swing +1.4 / −2.8 °C; handover fired 2 °C early (shared tolerance) so the last 2 °C climbed under P-push | Dedicated handover tolerance 0.5 °C (`COORDINATOR_HANDOVER_TOLERANCE_X10=5`) | Pending field confirmation |
| Hardening | Cubic ease shown to exceed the ramp rate (F-048) | Quadratic ease-out with planned-time extension | Not field-validated |
| Guards step 1 | Nuisance stalls and hold alarms | Stall window 180 → 300 s, hold band 5 → 4 °C, hold sustain 120 → 300 s | Pending field test |

Per-furnace values in `pid_values.md` (field-reported; the file is internally inconsistent, F-075):

| Unit | Kp | Ki | Kd | Out max | PID tick | SSR window | FF low | FF high |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 15 kW | 100 | 1 | 30 | 100 % (field build: 95 %) | 1000 ms | "1000 / 5000 ms?" | 80 °C → 8 % (file also says 7) | 150 °C → 10–12 %, uncalibrated |
| 25 kW | 95 | 1 | 30 | 100 % | 1000 ms | 5000 ms | 7 | 12 |

Findings from the logs that matter for the rewrite:

- Kd is ineffective here: the D-term is ±0.1–0.5 % because the chamber rises only ~0.05 °C/s at the ramp rates used.
- Steady ramp lag is ~0.8 °C, with a ~4 °C spike when a mid-program ramp starts. Higher Kp reduces the lag but worsens the arrival overshoot.
- Hold power rises with temperature. The FF high point must be measured at a 120–150 °C hold before higher-temperature holds are trusted.

## 8. Planned but not implemented

| Item | Rationale | State |
| --- | --- | --- |
| Velocity (rate) feedforward `FF_v = Kv · dSP/dt` during HEATING, fading to 0 at the corner (Kv ≈ 6 estimated from I ≈ 0.285 at 0.05 °C/s) | Remove the ramp-tracking lag without touching the tuned arrival | Proposed, not built |
| Replace the stall test with a **sustained under-temperature lag** guard (trip when SP − T > band for a sustain time), symmetric with the hold guard; delete `STALL_RATE_FRACTION_PCT`, `STALL_MIN_LAG_C` and the hard-coded `expected_rise > 1.0` | The 50 % rate fraction is the weak link; sustained lag self-scales with ramp rate | Queued after field validation |
| Lower overshoot threshold 20 → 15 °C (17–18 interim) | Faster runaway detection | Only after real arrival peaks are confirmed < 8–10 °C. Also fix F-057 first |
| Tighten hold band 4 → 2–3 °C | Earlier drift alarm | Only if measured dwell ripple stays < ~2 °C |
| Calibrate FF high point | Higher-temperature holds | Needs a measured 120–150 °C hold |

## 9. What a test-driven rewrite should pin down first

Write these as pure-logic unit tests before any RTOS code:

1. `ramp_ease_position`: q(0) = 0, q(1) = 1, monotonic, slope ≤ programmed rate everywhere, seam continuity, band ≥ span case, band = 0 case.
2. Phase detection and planned duration for heating, holding, cooling and zero-rate stages, including the near-target ramp case (F-080).
3. PID: proportional only; integral accumulation; anti-windup in both directions; derivative on measurement with a setpoint step (no kick); non-finite handling; both reset variants with FF on and off.
4. FF model: below, between and above the calibration points; degenerate points; clamp to out_max.
5. Guards against recorded field traces (replay a logged run and assert no nuisance trip), and synthetic stall, drift and overshoot traces.
6. Duration estimate and stage-change correction.
7. SSR windowing: on/off times for p ∈ {0, 0.001, 0.5, 1.0}, rounding to the tick, and inhibit mid-window.

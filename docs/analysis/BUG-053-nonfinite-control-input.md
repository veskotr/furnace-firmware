# BUG-053: Non-finite control values can reach the heater-demand boundary

- **Subsystem:** PID and heater controller
- **Severity:** high
- **Confidence:** confirmed at the PID/heater boundaries; normal MS9024 input is guarded
- **Status:** fixed; executable regression coverage pending
- **Affected files and symbols:** `components/pid_component/src/pid_component.c:pid_controller_compute/pid_controller_reset_for_setpoint`; `components/heater_controller_component/src/heater_controller_task.c:set_heater_target_power_level/get_heater_target_power_level`
- **Prerequisite or linked IDs:** F-001, F-021, F-053
- **Ready to fix:** implemented

## Observed behavior

Before this change, PID accepted non-finite setpoint, measurement, `dt`, and internal state. NaN bypasses comparison-based clamps, so it could be returned as demand. The heater setter also accepted NaN because neither `power_level < 0` nor `power_level > 1` is true for NaN; converting a non-finite target to the SSR on-time was unsafe.

## Expected behavior

No non-finite value may be retained in PID history, returned as PID demand, retained as heater target demand, or converted to a PWM window duration.

## Evidence

- `ms9024_read_float` already rejects decoded NaN/Inf and out-of-range values, lowering normal sensor-path reachability.
- The PID and heater public/internal boundaries previously had no equivalent checks, so future producers, state corruption, or another float path could bypass the Modbus guard.

## Implemented minimal fix

1. PID rejects non-finite setpoint, measurement, `dt`, integral, previous measurement, and calculated output; it resets state and returns finite zero.
2. PID bumpless reset rejects a non-finite setpoint and performs a normal reset.
3. Heater target setter rejects a non-finite command and clears the existing target before returning an error.
4. Heater PWM target reader treats any unexpected non-finite stored target as zero, preventing conversion to on-time.

No global fault state, HMI event, PID tuning, profile behavior, configuration, or hardware mapping changed.

## Regression test

Target-unit or mocked-component cases must verify NaN/+Inf/-Inf in each PID input/state returns finite zero and clears PID state; non-finite heater commands clear a prior positive demand; and a corrupted stored target produces zero on-time. No repository test harness or injection seam currently exists, so these cases are documented but not executed.

## Hardware validation

No powered validation is required to prove IEEE-754 guards themselves. Hardware validation is still required for the broader stale/invalid-sensor safety contract (F-001); this change does not establish it.

## Remaining uncertainty

This stops non-finite propagation but does not reject stale, plausible, finite temperatures, synchronize temperature ownership, or provide a direct independent off path for every invalid-input condition.

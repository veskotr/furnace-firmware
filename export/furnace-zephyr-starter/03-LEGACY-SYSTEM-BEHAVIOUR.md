# 03 — The legacy system: final, field-used behavior

This describes the old firmware as built and used in the field at hand-over (2026-10-13). Superseded intermediate designs are mentioned only where they teach something. Where the final behavior is a known defect, it is marked **(defect: F-xxx)**; the full description is in [07-LESSONS-LEARNED.md](07-LESSONS-LEARNED.md). The new project must **not** reproduce marked defects.

## 1. The machine

| Item | Legacy facts | Evidence |
| --- | --- | --- |
| Application | Electric furnace for composite (prepreg) curing; program targets limited to 20–170 °C | legacy-source configuration |
| Units in service | Two sizes, 15 kW and 25 kW, each with its own PID values | field-reported |
| Controller | ESP32 dual core, ESP-IDF 5.5, FreeRTOS at 100 Hz tick (10 ms) | legacy-source |
| Flash | Unknown to the team; the field build configuration claimed 8 MB. **The new board has 16 MB (D-04).** | — |
| Temperature sensing | Five Pt100 RTDs, each on its own MS9024 transmitter, Modbus RTU over RS-485, slave addresses 1–5. **Likely replaced by custom sensors (D-10).** | legacy-source |
| Heating actuation | One SSR (time-proportioned on/off) plus one contactor (enable and isolation) | legacy-source |
| Fan | One on/off output; the fan both cools and distributes heat (D-12) | legacy-source, decided |
| Operator interface | Nextion touch panel on a UART: program editor, run control, settings, program files on the panel's SD card, panel real-time clock. **Not hosted by the new controller (D-01).** | legacy-source |
| Service console | Serial console with `help`, `log-ls` (list crash logs), `log-file <name>` (dump one) | legacy-source |

### Legacy I/O map (for orientation only; the new board's pins come from its schematic, D-14)

| Signal | Legacy pin | Notes |
| --- | --- | --- |
| SSR control | GPIO 21, output, internal pull-down | Assumed active-high; never verified against a schematic |
| Contactor control | GPIO 22, output, internal pull-down | Assumed active-high; never verified |
| Fan | GPIO 19, output | Written directly, outside the GPIO lock |
| RS-485 TX / RX / DE | GPIO 27 / 26 / 25, UART2 half-duplex | 9600 8N1, 300 ms response timeout |
| Panel UART | GPIO 32 / 33, 115200 | |
| Run indicator | none in production | Its first default pin was the contactor pin, a dangerous collision |

The pin states between reset and software configuration depended on unknown external pull resistors.

## 2. Software structure

Components communicated through three mechanisms:

- one private event loop (single task, queue of 32 events);
- one command queue with a single consumer task (depth 10);
- direct function calls into the heater component for safety inhibits.

```
 sensors x5 --RS-485--> modbus master --> sensor device (cached value + last-good tick)
                                              ^ polled by device-manager task (~1 s cycle)
 device manager --"devices updated" event--> temperature processor task
 temperature processor --temperature event (only when valid)--> coordinator, fan, HMI
                       --validity event (always)--> coordinator
 HMI --command--> command dispatcher --> coordinator (start/pause/resume/stop/manual target)
 coordinator control task (1 s PID tick) --> profile tick --> PID --> "set heater power" command
 command dispatcher --> heater handler --> target power --> heater PWM task --> SSR pin
 coordinator / freshness timer --direct call--> heater inhibits --> SSR off + contactor off
 coordinator --status and lifecycle events--> fan controller, HMI
```

### Event subscriptions at hand-over

| Event | Publisher | Consumers |
| --- | --- | --- |
| Temperature (float, only when the aggregate is valid) | temperature processor | coordinator, fan, HMI |
| Validity snapshot (always) | temperature processor | coordinator only |
| Devices updated | device manager | temperature processor |
| Coordinator status and lifecycle | coordinator | fan, HMI |
| Heater toggled (every SSR write, infinite wait) | heater | **nobody** (defect: F-062) |
| Health heartbeat (infinite wait) | most tasks | **nobody**; health monitor disabled |
| Furnace error | heater, coordinator, processor, device manager | **nobody** (defect: F-035) |

## 3. Startup sequence

1. Service console.
2. Settings storage (NVS). On "no free pages" or "new version" the **whole partition was erased**, losing service hours (defect: F-073).
3. Logger: queue, task, console commands, crash-log filesystem mount.
4. Event loop and command dispatcher (failure aborts startup).
5. Heater: GPIOs configured as outputs with pull-down, SSR low, contactor low, PWM task started.
6. Coordinator: **asserts the sensor-data inhibit**, so heating is impossible until valid temperature arrives; creates the freshness timer; subscribes to temperature events.
7. Modbus master, fan controller, HMI, device manager. Failures here were only logged.
8. After a fixed 2 s delay, the temperature processor creates the five sensors (about 10 s of further delays), then starts.

Lesson: the actuators were up before sensing existed. That was safe only because step 6 started inhibited.

## 4. Method of operation

### 4.1 Idle

- Contactor off, SSR off. The sensor-data inhibit is held until three consecutive valid aggregates arrive.
- Fan: on above the upper threshold, off below the lower one (field build 43 / 41 °C).
- The panel shows the chamber temperature (valid aggregates only) and the service-hours counter.

### 4.2 Temperature acquisition (about once per second)

1. The device-manager task updates each sensor in turn, waiting 100 ms between sensors. A sensor update reads the process value, decodes and range-checks it, then caches the value and the current tick. A timed-out read costs 300 ms and leaves the cache untouched.
2. After the pass, it posts "devices updated".
3. The temperature processor keeps sensors whose last successful read is younger than **5 s** and averages them. It warns if max − min exceeds a threshold (field build 5 °C). This was a warning only; that policy is replaced by D-07.
4. Valid means processing succeeded **and** at least N − 2 sensors are fresh (3 of 5).
5. It posts the temperature event (when valid) and the validity snapshot (always).
6. The coordinator re-arms a one-shot **freshness timer** for the remainder of the 5 s budget on each valid snapshot. On an invalid snapshot, or when the timer fires, it inhibits the heater directly (SSR and contactor off) and pauses the run.

### 4.3 Running a program

1. The operator starts a program on the panel. The panel validates it against device limits and sends the program and the cooldown rate to the controller. **The controller trusted that validation** (defect: F-076).
2. The coordinator refuses to start without a post-boot valid temperature, while the sensor inhibit is held, or with an aggregate older than 5 s.
3. It copies the program and estimates the duration from the current temperature. It then resets the PID, loads the profile, and creates the control task and the 1 s timer. Only after that does it release the temporary control inhibit and command "clear" and "start" (contactor on).
4. Every second:
   - apply any pending manual-target change;
   - run the profile tick (stage machine and setpoint);
   - check the guards (overshoot, stall, hold deviation);
   - run the PID (not in COOLING or COOLDOWN) and queue "set power";
   - publish status.
5. The heater PWM task turns the SSR on for `power × 5 s`, then off for the rest of each 5 s window. New power takes effect only at the next window (F-078).
6. Stage behavior:
   - **HEATING** ramps the setpoint from the *measured* start temperature at the programmed rate, softens the last 6 °C, and ends when the chamber is within 0.5 °C of target.
   - **HOLDING** lasts its programmed minutes.
   - **COOLING** turns the heater off and ends when the chamber is within 2 °C above target.
   - After the last stage, **COOLDOWN** keeps the heater off and the fan on, and completes below 40 °C.
7. Completion, operator stop or emergency stop asserts the control inhibit (SSR and contactor off directly), resets the PID, and tears down the control task and timer.

### 4.4 Pause, resume and faults

| Trigger | Action | What the operator saw |
| --- | --- | --- |
| Operator pause | Control inhibit; outputs off; profile time frozen | "Paused" |
| Operator resume | Bumpless PID reset; inhibit released | "Running" |
| Stall in HEATING (300 s window) | Fault pause plus error with temperature, setpoint, stage and duration | "Heating stall S2: 120/150C after 5min" |
| Hold deviation (> 4 °C for 300 s) | Fault pause plus error | "Hold off-target …" |
| Overshoot (T > setpoint + 20 °C, field build) | Inhibit; error event nobody received; **"profile completed"** | **"Completed"** (defect: F-058). Also trips at the start of any cooling step larger than 20 °C (defect: F-057) |
| Sensor aggregate invalid or expired | Sensor inhibit plus pause; **no event**; never auto-resumes | Still "Running" with a frozen temperature (defects: F-059, F-060) |
| SSR GPIO write failure | Permanent inhibit until reboot; contactor off; error event nobody received | Nothing |

### 4.5 Manual mode (final behavior: defective)

The operator sets a target (20–170 °C) and a rate (0.1–3.0 °C/min). The panel builds a one-stage program with a 7-day duration. On reaching the target, the profile ran out of stages, entered COOLDOWN and **switched the heater off** (defect: F-064). Live changes re-targeted from the current temperature, but ramped faster than requested (defect: F-065). D-09 requires a manual mode that holds until stopped.

## 5. Timing summary

| Quantity | Value |
| --- | --- |
| RTOS tick | 10 ms |
| Sensor poll cycle | ~1000 ms + 100 ms per sensor + up to 300 ms per timed-out read |
| Aggregate freshness budget | 5000 ms |
| Recovery after invalidity | 3 consecutive valid aggregates |
| PID period | 1000 ms |
| SSR window | 5000 ms; demand sampled once per window |
| Stall window | 300 s |
| Hold-deviation sustain | 300 s |
| Watchdog | System default, 5 s, idle tasks only; no application task watched |

## 6. Responsibilities the panel (HMI) carried, all moving to the new controller (D-01)

### 6.1 Operator commands

| Group | Function | Allowed while running |
| --- | --- | --- |
| Run | Start (validate, then send program + cooldown rate); pause/resume toggle; stop with confirmation ("can't resume") | yes |
| Manual | Toggle manual; target ± and direct entry; rate ± and direct entry; live update while running | yes |
| Fan | "Max/Silent" toggle: stored and displayed but **never applied** (defect: F-070) | yes |
| Navigation and programs | Browse, select, add, edit, page through stages, save (with validation), delete with confirmation, autofill, projected-curve graph | no ("Stop program first") |
| Settings | Set time and date (stored in the panel's clock), ambient reference temperature (0–170 °C), cooldown rate | no |
| System | Restart and factory reset: both inhibit the heater first and refuse if that fails. Factory reset deletes the known programs and erases settings but keeps service hours. | no |

Destructive commands required an exact command string; everything else was matched by substring.

### 6.2 Domain logic that lived in the panel component

- **Program model and editing**, including the per-stage set-flags.
- **Save-time validation:**
  - name: letters, digits and spaces, no commas, 31 characters at most;
  - 1–15 stages;
  - target 20–170 °C;
  - heating stages: time 1…10080 min and rate −3.0…+3.0 °C/min;
  - each stage's math consistent within ±2 °C and ±1 min, starting from the stored ambient reference;
  - total time 1…10080 min;
  - cooling stages skip the time, rate and math checks.
- **Run-time validation** (device limits only). It lived in the panel, not the controller.
- **Autofill:** time from rate, rounded up; rate from time only when the division is exact, otherwise a suggestion.
- **Duration estimate and projected graph.** A second copy of the duration math, plus a linear graph that did not match the eased runtime ramp.
- **Remaining-time extension during pause.**
- **Service-hours counter:** +1 s every second while a program is active, pause included. Saved at most once a minute and on stop/complete; survives factory reset. A bounded service override existed.
- **Displayed power** = PID output × nominal heater kW (not measured).
- **Operator vocabulary:** "S2/5 RAMP 150C", "S3/5 HOLD 150C 10m", "S4/5 COOL 80C", "Cooldown", "Completed", "Paused", "Stopped", plus fault texts "Heating stall S2: 120/150C after 5min" and "Hold off-target …".

### 6.3 Persistence on the panel path

| Data | Store |
| --- | --- |
| Ambient reference °C, service hours (s), fan-mode preference, cooldown rate (0.1 °C/min units) | Controller NVS namespace `user_prefs` (`ambient_c`, `op_time_s`, `fan_mode`, `cool_rate`) |
| Names of programs saved through the firmware | Controller NVS namespace `prog_registry` (needed because the panel could not list its directory) |
| Program library | Panel SD card, `<name>.prg`, ≤ 4096 bytes, written to a temporary file then renamed, read back to verify |
| Wall-clock time | Panel RTC only. **The controller had no clock**, so logs had uptime only |

Program file format (text, one stage per line, 1-based stage index):

```
name=<program name>
stage=<n>,t=<minutes>,target=<°C>,tdelta=<minutes>,delta_x10=<rate in 0.1 °C/min>
```

Whether existing field programs are imported is deferred topic T-13.

### 6.4 Display contract

Per PID tick, the controller sent status: temperature, setpoint, power 0–1, elapsed ms, total ms, active-stage ordinal / count, phase, and hold time remaining. It also sent lifecycle events (started, paused, resumed, stopped, completed) and errors (code, temperature, setpoint, stage, fault duration). The new protocol is deferred topic T-02; its content is T-01.

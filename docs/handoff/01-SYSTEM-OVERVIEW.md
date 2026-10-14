# 01 — System overview and method of operation

## The machine

| Item | Current facts | Evidence |
| --- | --- | --- |
| Application | Electric furnace for composite (prepreg) curing; program targets limited to 20–170 °C | config `NEXTION_MIN/MAX_TEMPERATURE_C`; Kconfig help in `coordinator_component/Kconfig` |
| Units in service | Two furnace sizes: 15 kW and 25 kW (each has its own PID values) | `components/pid_component/pid_values.md` (field-reported) |
| Controller | ESP32 (dual core), ESP-IDF 5.5.x, FreeRTOS at 100 Hz tick | config `IDF_TARGET`, `FREERTOS_HZ=100` |
| Flash | Field build: 8 MB, custom partition table with LittleFS `storage` partition. Tracked defaults: 2 MB, single-app table (mismatch, see F-067) | config; `partitions.csv` |
| Temperature sensing | Five Pt100 RTDs, each on its own MS9024 transmitter, Modbus RTU over RS-485 at slave addresses 1–5 | source `main.c:init_temp_processor(5)`, `temp_sensor_device_core.c:temp_sensor_create` |
| Heating actuation | One SSR (time-proportioned) plus one contactor (enable/isolation) | source `heater_controller.c` |
| Fan | One on/off fan output | source `fan_controller.c` |
| Operator interface | Nextion touch panel on UART1 (program editor, run control, settings, panel SD card for program files, panel RTC) | source `components/nextion_hmi` |
| Service console | UART0 `esp_console` with `help`, `log-ls`, `log-file` | source `debug_console.c`, `logger_cli.c` |

### Physical I/O map (Kconfig defaults equal the tracked and field values)

| Signal | GPIO | Direction | Driven by | Notes |
| --- | --- | --- | --- | --- |
| SSR control | 21 | out, internal pull-down | `heater_controller.c:toggle_heater` | Assumed active-high; polarity unverified (F-042) |
| Contactor control | 22 | out, internal pull-down | `heater_controller.c:start_heater/stop_heater` | Assumed active-high; polarity unverified |
| Fan | 19 | out, no pulls | `fan_controller.c:fan_set` (direct `gpio_set_level`, bypasses the GPIO mutex) | |
| RS-485 TX / RX / DE | 27 / 26 / 25 | UART2 half-duplex RS-485 | `modbus_master_core.c` | 9600 8N1, 300 ms response timeout (hard-coded) |
| Nextion TX / RX | 32 / 33 | UART1, 115200 | `nextion_transport.c` | Out of scope for the rewrite |
| Console | UART0 | | `debug_console.c` | |
| Run indicator | none in production | | `run_indicator.c` (test-only; excluded from build) | Defaulted to GPIO 22 originally, which collided with the contactor (F-016) |

Reset and boot GPIO state is set by hardware until `init_heater_controller()` runs; the deployed pull resistors and output polarity are unknown (F-042, F-047).

## Software structure (current)

Components live in `components/`; `main/main.c:app_main` starts them. Production data flows through:

- a private `esp_event` loop (`event_manager`), one task, queue depth 32;
- a command queue with a single consumer task (`commands_dispatcher`), depth 10;
- direct calls into the heater component for safety inhibits.

```
 MS9024 x5 --RS-485--> modbus_master --> temp_sensor_device (per-sensor cache + last-good tick)
                                              ^ polled by device_manager task (1 s cycle)
 device_manager --DEVICE_MANAGER_UPDATED_EVENT--> temperature_processor task (notify)
 temperature_processor --TEMP_PROCESSOR_EVENT(float, only when valid)--> coordinator, fan, HMI
                       --TEMP_PROCESSOR_EVENT(validity snapshot, always)--> coordinator
 HMI (Nextion) --command_t--> commands_dispatcher --> coordinator handler (start/pause/resume/stop/manual)
 coordinator control task (1 s PID tick timer) --> profile_tick --> pid_compute --> command HEATER_SET_POWER
 commands_dispatcher --> heater handler --> target power (mutex) --> heater PWM task --> SSR GPIO
 coordinator / expiry timer --direct call--> heater inhibits --> SSR off + contactor off
 coordinator --COORDINATOR_EVENT--> fan controller, HMI
```

## Startup sequence (source `main/main.c:app_main`)

1. Debug console (failure logged, continues).
2. NVS init; on `NO_FREE_PAGES` or `NEW_VERSION_FOUND` the whole NVS partition is **erased** and re-initialised (F-073).
3. Logger (queue, pinned task on core 1, console commands, LittleFS mount of `storage` at `/crash_dumps`, restart-time RTC snapshot recovery).
4. Event manager and registry; failure **returns from `app_main`**.
5. Command dispatcher; failure returns.
6. Heater controller: registers the command handler, configures GPIO 22 and GPIO 21 as outputs with pull-down, drives SSR low and contactor low, starts the PWM task. Failure returns.
7. Coordinator: creates mutexes, **asserts the recoverable sensor-data inhibit** (heating is impossible until valid temperature arrives), creates the expiry timer, registers its command handler, subscribes to temperature events. Failure returns.
8. Health monitor: commented out (F-036).
9. Modbus master on UART2 (failure logged, continues).
10. Fan controller (void, unverified).
11. Nextion HMI, if `CONFIG_NEXTION_HMI_ENABLED`.
12. Device manager task (failure logged, continues).
13. Fixed 2 s delay, then `init_temp_processor(5)`, which creates five sensors with 2 s of delays each (≈10 s), then starts the processor task.
14. `app_main` loops on a 10 s delay.

Result: actuation components are fully up before sensing exists. That is safe only because step 7 starts with the sensor-data inhibit asserted.

## Method of operation

### Idle

- Contactor off, SSR off, coordinator holds the sensor-data inhibit until three consecutive valid aggregates arrive (`COORDINATOR_SENSOR_RECOVERY_VALID_AGGREGATES=3`).
- The fan runs in auto mode: on at ≥ `FAN_CONTROLLER_AUTO_ON_C`, off below `FAN_CONTROLLER_AUTO_OFF_C` (field 43/41 °C; tracked 35/32 °C).
- The HMI shows chamber temperature (only from valid aggregates) and the operating-hours counter.

### Temperature acquisition (every ~1 s)

1. The device manager task updates each RUNNING device in order, waiting 100 ms between devices. An MS9024 update reads two holding registers at 728 (PV), decodes the float as CDAB, rejects NaN/Inf, `-0.0` (no reading yet) and values outside −200…1500 °C, then caches the value and the current tick.
2. After the pass it posts `DEVICE_MANAGER_UPDATED_EVENT` (unbounded wait).
3. The temperature processor wakes, keeps sensors whose last successful read is newer than 5 s, and averages those. It warns if max−min exceeds `TEMP_DELTA_THRESHOLD` (warning only, by explicit field requirement). Validity = processing OK **and** fresh count ≥ max(1, N−2), so 3 of 5 sensors.
4. It posts the legacy float event (only when valid) and the validity snapshot (always).
5. The coordinator, on a fresh valid snapshot, re-arms a one-shot expiry timer for the remaining part of the 5 s budget. On an invalid snapshot, or when the timer fires, it directly inhibits the heater (SSR and contactor off) and pauses any running profile.

### Running a program

1. The operator starts a program on the HMI. The HMI runs `program_validate_draft_for_run` (device limits only), then sends `COORDINATOR_START_PROFILE` with a by-value copy of the program and the configured cooldown rate.
2. The coordinator refuses to start without a valid post-boot temperature, while the sensor-data inhibit is held, or when the aggregate is older than 5 s.
3. It copies the program, estimates duration from the current temperature, resets the PID, loads the profile, creates the control task and the 1 s PID tick timer, releases the temporary control inhibit, then queues `HEATER_CLEAR` and `HEATER_START` (contactor on).
4. Each PID tick: apply any pending manual-target change, run `profile_tick` (stage state machine, setpoint), check guards (overshoot, stall, hold deviation), run the PID (except in COOLING/COOLDOWN), queue `HEATER_SET_POWER`, post a status event.
5. The heater PWM task turns the SSR on for `power × 5000 ms` and off for the rest of each 5 s window. The new power is picked up only at the start of the next window.
6. Stages: a HEATING stage ramps the setpoint at the programmed rate from the **measured** start temperature, softens the last 6 °C, and ends when the chamber is within 0.5 °C of target. A HOLDING stage lasts `t_min` minutes. A COOLING stage turns the heater off and ends when the chamber falls within 2 °C of target. After the last stage the program enters COOLDOWN (heater off, fan on) and completes when the chamber is below 40 °C.
7. Completion, operator stop, or emergency stop asserts the control inhibit (SSR and contactor off directly), resets the PID, and tears down the control task and timer.

### Pause, resume and faults

| Trigger | Action | Operator sees |
| --- | --- | --- |
| Operator pause | Control inhibit, SSR/contactor off, profile time frozen | "Paused" |
| Operator resume | Bumpless PID reset, release control inhibit | "Running" |
| Stall (HEATING, 300 s window field) | Fault pause plus error event with temperature, setpoint, stage, duration | "Heating stall S2: 120/150C after 5min" |
| Hold deviation (> 4 °C for 300 s) | Fault pause plus error event | "Hold off-target …" |
| Overshoot (> setpoint + 20 °C field / 30 °C tracked) | Control inhibit, `FURNACE_ERROR_EVENT` (no subscriber), **`PROFILE_COMPLETED`** | **"Completed"** (F-058) |
| Sensor aggregate invalid or expired | Sensor-data inhibit plus pause; no paused or error event (F-059) | Still "Running", temperature frozen |
| SSR GPIO write failure | Permanent heater inhibit until reboot, contactor off, `FURNACE_ERROR_EVENT` | Nothing (no subscriber) |

### Persistence

- NVS `user_prefs`: ambient temperature for program validation, operating-hours counter (seconds), fan-mode display preference, cooldown rate.
- NVS `prog_registry`: names of programs saved through the firmware (for factory-reset deletion).
- Program files on the Nextion panel SD card as text `.prg` files.
- LittleFS `storage` at `/crash_dumps`: crash-log files (the capture path is mostly not wired, F-068).
- RTC slow memory: last-20-log snapshot written only on `esp_restart()`.

### Timing summary

| Quantity | Value | Source |
| --- | --- | --- |
| RTOS tick | 10 ms | `FREERTOS_HZ=100` |
| Sensor poll cycle | 1000 ms nominal, plus 100 ms per device and up to 300 ms per timed-out read | `DEVICE_MANAGER_UPDATE_INTERVAL_MS`, `device_manager_task.c`, `modbus_master_core.c` |
| Aggregate freshness budget | 5000 ms | `COORDINATOR_SENSOR_AGGREGATE_STALE_TIMEOUT_MS` |
| Recovery | 3 consecutive valid aggregates | `COORDINATOR_SENSOR_RECOVERY_VALID_AGGREGATES` |
| PID tick | 1000 ms | `COORDINATOR_PID_TICK_INTERVAL_MS` |
| SSR window | 5000 ms (PID output sampled once per window) | `HEATER_WINDOW_SIZE_MS` |
| Stall window | 300 s field / 180 s tracked | `COORDINATOR_STALL_CHECK_MS` |
| Hold-deviation sustain | 300 s | `COORDINATOR_HOLD_VIOLATION_MS` |
| Task watchdog | ESP-IDF default, 5 s, idle tasks only (no application task subscribed) | config `ESP_TASK_WDT_*` |

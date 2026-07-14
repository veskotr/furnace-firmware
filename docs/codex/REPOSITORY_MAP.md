# Furnace firmware repository map

Last source-mapped: 2026-07-13. Pulled-change review: `16467e0` (feature commit `6741c72`). Source and resolved build configuration are authoritative; update both this file and `repository-map.yaml` after architectural changes.

## Repository shape

| Path | Purpose |
| --- | --- |
| `main/` | `app_main()` entry point and top-level component dependencies |
| `components/` | ESP-IDF components: control, devices, communications, UI, drivers, and operations |
| `components/*/include/` | Public component interfaces |
| `components/*/src/` | Private implementation and state |
| `docs/codex/` | Agent-efficient architecture, standards, and Codex compatibility context |
| `docs/analysis/` | Evidence-backed findings; historical reports remain leads |
| `docs/templates/` | Bug, feature, ADR, and validation artifacts |
| `.codex/agents/` | Discoverable project agent definitions |
| `.agents/skills/` | Discoverable project workflow skills |
| `tools/codex/` | Repository setup verification tooling |
| `build/`, `managed_components/`, `sdkconfig*` | Generated/local ESP-IDF state; not architectural source |

Platform: ESP-IDF C, FreeRTOS, ESP32, private `esp_event` loop. Root build entry: `CMakeLists.txt`; application entry: `main/main.c:app_main`.

## Initialization sequence

`app_main()` initializes in this order:

1. `debug_console_init()` — failure logged; startup continues.
2. NVS — erase/retry on version/full errors; other failure logged; startup continues.
3. `logger_init()`.
4. `event_manager_init()` and `event_registry_init()` — failure returns from `app_main`.
5. `commands_dispatcher_init()` — failure returns.
6. `init_heater_controller_component()` — failure returns.
7. `init_coordinator()` — failure returns.
8. Health monitor initialization is commented out.
9. `modbus_master_init()` — failure logged; startup continues.
10. Run indicator, fan, and Nextion HMI initialization — void/unverified.
11. `device_manager_init()` — failure logged; startup continues.
12. Fixed two-second delay, then `init_temp_processor(5)` — failure logged; startup continues.
13. `app_main` remains in a ten-second delay loop.

This mixed fail-fast/log-and-continue policy is safety-relevant because control and output components can exist while sensing or communications initialization failed.

## Runtime contexts

| Context | Owner / creation | Wake/blocking behavior | Shared-state seam |
| --- | --- | --- | --- |
| App main task | ESP-IDF; `main/main.c:app_main` | Ten-second loop after startup | Initialization order only |
| Debug console task | `debug_console.c:debug_console_init` | Console polling/blocking | Global running flag/handle; no join |
| Logger task, core 1 | `logger_core.c:logger_init` | Blocks on logger queue | Static queue/storage state |
| Private event-loop task | `event_manager.c:event_manager_init` | Serialized event callbacks | Event payload contracts/subscription lifetime |
| Command dispatcher | `commands_dispatcher_task.c:commands_dispatcher_task` | Queue receive, two-second timeout; re-entrant submissions execute inline | Sole consumer; inline path prevents self-deadlock, nested handler depth remains a risk |
| Heater PWM task | `heater_controller_task.c:init_heater_controller_task` | Five-second default SSR window; task notification interrupts wait | Mutex-protected target; separate permanent SSR-failure and recoverable sensor-data inhibits |
| Coordinator control task | `coordinator_component_heater_controller.c:start_heating_profile` | Notified by one-second default ESP timer; pauses on invalid aggregate | Profile, PID, mutex-protected temperature, recovery counter, inhibit policy |
| Device manager task | `device_manager_task.c:init_device_manager_task` | Periodic Modbus updates and notification-shortened wait | Device table/state/lifecycle |
| Temperature processor task | `temperature_processor_task.c:init_temp_processor_task` | Device-update notification | Sensor array, sample buffer, context lifetime |
| HMI coordinator task | `hmi_coordinator.c:hmi_coordinator_init` | Queue receive every 50 ms | Sole display command/deferred-state owner |
| Nextion RX task | `nextion_rx_task.c:nextion_rx_task_start` | UART polling/read | UART mutex and transfer-active flags |
| Run-indicator task | `run_indicator.c:run_indicator_init` when enabled with a valid non-colliding pin | 200 ms polling/blink | Unsynchronized `s_mode` when enabled |
| Fan callbacks | `fan_controller.c:fan_controller_init` | Serialized event-loop callbacks | Static fan mode/state |
| Optional/inactive | health monitor, legacy SPI temperature monitor, transmitter diagnostics | Not started by current `app_main` | Compiled code is not active behavior |

No application hardware ISR registration was found. The PID timer callback uses `ESP_TIMER_TASK`, despite an ISR-context comment. UART drivers have internal interrupts; application code reads from tasks.

## Communication and synchronization inventory

| Primitive | Owner | Producers / readers | Consumer / writer | Failure/lifetime notes |
| --- | --- | --- | --- | --- |
| Private ESP event queue | `event_manager` | All event publishers | Private loop callbacks | Blocking wrapper can wait forever; unsubscribe before context destruction |
| Dispatcher `command_queue` | `commands_dispatcher` | HMI, coordinator, other callers | Sole dispatcher task | External submissions use `portMAX_DELAY`; dispatcher-task self-submissions execute inline |
| Logger queue | `logger_component` | Logging macros/callers | Logger task | 100 ms producer timeout then drop; no shutdown path |
| HMI command queue | `nextion_hmi` coordinator | Event bridges, RX/UI | HMI coordinator task | Event bridges send nonblocking and silently drop when full |
| Heater `power_mutex` | `heater_controller` | Dispatcher handler/task | Target power/state access | Does not protect all lifecycle fields |
| GPIO mutex | `gpio_master_driver` | Heater and callers using wrapper | Serialized GPIO calls | Does not prevent two components claiming one pin |
| SPI mutex | `spi_master_component` | Legacy SPI clients | Serialized SPI bus | Legacy path inactive |
| Logger storage mutex | `logger_component` | Logger/storage/CLI paths | RAM ring/files | Shutdown callback assumptions remain |
| Nextion UART recursive mutex | `nextion_hmi` transport | RX, send, storage, file reader | Serialized UART access | Transfer-active volatile flags are not locks |
| Program-model recursive mutex | Nextion program model | HMI handlers/storage | Draft/program accessors | Several scalar preferences remain outside mutex |
| Legacy ring mutex/event group | `temperature_monitor_component` | Legacy task/processors | Legacy only | Compiled but not initialized |
| Task notifications | heater/coordinator/device/temp/health | Timers, stop functions, callbacks | Respective task | Notification is wakeup, not a complete join protocol |

No application counting/binary semaphore or explicit spinlock was found.

## Major modules

| Module | Responsibility / public interface | Owned state and execution | Dependencies / hardware | Safety and coupling debt |
| --- | --- | --- | --- | --- |
| `event_manager` + `event_registry` | Private event loop; `event_manager_*`; event bases/IDs | Global loop handle; event task callbacks | ESP event | Global singleton; payload/lifetime contracts are distributed |
| `commands_dispatcher` | Queued `command_t` routing; register/dispatch APIs | Queue, handler table, task, atomic running state, exit acknowledgement | FreeRTOS queue | Re-entrant handler submissions execute inline; external producer shutdown remains uncoordinated |
| `coordinator_component` | Program commands/status, control task, heater command production; sensor-data pause/recovery; boot/recovery temperature gate | Global program/profile/current temp/state/timer/task, temperature mutex, exit acknowledgement, recovery counter | Dispatcher, events, PID/profile, heater inhibit API | Temporary sensor-count-minus-two quorum; HMI fault presentation and callback quiescence remain open |
| `temperature_profile_controller` | `load_heating_profile`, `profile_tick`, profile state/status and eased heating setpoint/handover | File-static profile context and tick counter | Common profile types | Lifetime/tick ownership coupled to coordinator; runtime ease-out extends the final ramp band while legacy/UI graph remains linear |
| `pid_component` | `pid_controller_compute/reset/reset_for_setpoint/set_adaptive_enabled` | File-static integral/history/adaptive/feedforward flag | Kconfig, logger | Single implicit instance; rejects non-finite inputs/state/output by reset/zero demand; optional feedforward and adaptive clamp can both be enabled |
| `heater_controller_component` | Contactor/SSR commands and time-proportional output | Global context; `power_mutex` protects target demand and permanent, sensor, and temporary control inhibit reasons; PWM task acknowledges exit before teardown | GPIO master; contactor GPIO 22, SSR GPIO 21 defaults | Inhibits force direct SSR/contactor off and reject reauthorization; SSR GPIO failure remains permanent; physical result needs bench validation |
| `device_manager` | Device abstraction, state, periodic updates | Global device array/task/atomic running state/count; worker exit acknowledgement | FreeRTOS, events | Stop waits for worker exit; API mutation and producer/event lifecycle remain open |
| `modbus_master` | ESP-Modbus RTU init/read/write helpers | ESP-Modbus master instance | UART2 TX27/RX26/DE25, 9600 defaults | No application serialization contract documented; timeout fixed at 300 ms |
| `temp_sensor_device` | MS9024 abstraction and cached temperature | Static five-slot context pool; per-device cache and successful-read tick | Modbus registers PV 728, ID 127 | Per-device metadata feeds processor freshness; device lifecycle remains cross-task |
| `temperature_processor_component` | Read fresh sensor samples, average/warning checks, publish legacy temperature plus validity snapshot | Global context, sensor array/ticks, buffer, task with exit acknowledgement | Device events, furnace events | Temporary quorum derived from sensor count; per-board mapping/quorum remains future work; producer/task shutdown is not fully coordinated |
| `temperature_monitor_component` | Legacy MAX31865/SPI path | Global context, task, ring/event group | SPI/MAX31865 | Not initialized; duplicate temperature architecture increases drift risk |
| `nextion_hmi` | UART protocol, UI events, program model, panel storage | RX and coordinator tasks, queues, UART/program mutexes; complete-read/atomic-draft load path; bounded packet-NAK and temporary-file replacement save paths | UART1 TX32/RX33, panel SD/FileStream, NVS | Many submodules/global flags; telemetry drop and final rename/power-loss storage risk remain |
| `fan_controller_component` | Auto/program fan policy | Event-loop-owned static state | Fan GPIO19 | No airflow feedback/interlock; program fan intent split from coordinator fields |
| `run_indicator` | Program state indication; disabled when unset or pin collides with contactor | Task and event-written mode | No GPIO by default; configured indicator GPIO only after collision guard | Indicator is intentionally unavailable until a schematic-approved pin is configured; mode remains unsynchronized when enabled |
| `logger_component` | Async ESP logging, LittleFS/RTC crash records, CLI | Queue/task, ring/files/mutex | Core 1, LittleFS `/crash_dumps`, RTC memory | Drops under pressure; furnace-error events not integrated |
| `error_manager` | Error descriptor lookup | Static descriptor table | Common errors | No descriptor registrations found; not an active mitigation path |
| `health_monitor` | Heartbeat table and task watchdog | Event table, health task | ESP task WDT | Disabled; if enabled it does not directly inhibit heater |
| `gpio_master_driver` | Serialized GPIO setup/set | Global mutex | ESP GPIO | Pin ownership is not validated across components |
| `spi_master_component` | Serialized SPI wrapper | Global bus/mutex | ESP SPI | Primarily legacy/inactive |
| `heating_program_validation` | Program bounds/shape validation | Pure validation | Common profile types | Confirm every entry path invokes it |
| `debug_console` | UART0 console/commands | Task/global state | UART0 | Shutdown lacks join; operational only |
| `transmitter_diagnostics`, `time_component` | Diagnostic dump/time utilities | Optional/static | UART/time | Diagnostics not active at startup |

## End-to-end flows

### Temperature acquisition

`device_manager_task` → `temp_sensor_update` → successful-read tick/cache → `DEVICE_MANAGER_UPDATED_EVENT` → temperature task notification → fresh-sample filtering/quorum → legacy float plus validity `TEMP_PROCESSOR_EVENT` → coordinator inhibit/pause/recovery and fan/HMI telemetry.

Key debt: physical-read success, cache age, validity, contributing sensor identity, and batch quorum are not carried with the published value.

### Profile and control

Nextion run handler → coordinator command in dispatcher queue → `start_heating_profile` resets PID, loads profile, queues heater clear/start, creates control task and periodic timer → timer notifies control task → `profile_tick` calculates phase/eased heating setpoint and handover → PID computes 0..1 demand (optional feedforward) → coordinator queues heater set-power → dispatcher invokes heater handler → PWM task windows SSR output. Sustained hold deviation or a ramp-stall condition posts enriched error data and pauses through the same queued `kill_heater` path.

Sensor invalidity now calls the heater component's direct recoverable inhibit, which forces SSR/contactor off before coordinator pause; normal pause/stop still retain queued `kill_heater` behavior and F-003 remains open.

### Physical outputs

- Contactor: heater commands → `start_heater`/`stop_heater` → GPIO 22 default.
- SSR: target power → heater PWM task → `toggle_heater` → GPIO 21 default.
- Fan: temperature/coordinator event callbacks → GPIO 19 default.
- Run indicator: coordinator event → shared mode → indicator task → GPIO 22 default, colliding with contactor defaults.

### HMI and persistence

Nextion RX task parses lines → HMI command queue → coordinator task/handlers → dispatcher or model/storage APIs. System events bridge into the same queue for display updates; error payloads now carry temperature, setpoint, stage, and elapsed-fault context but may still be independently dropped. User preferences are stored in NVS namespace `user_prefs`; program files live on the Nextion panel SD through FileStream/twfile. Factory reset erases ESP NVS then restores operational time; the optional service-time override writes a configured value at every boot while enabled.

### Logging, faults, watchdog, recovery

Logger producers → bounded queue → pinned logger task → ESP log and LittleFS/RTC records. `FURNACE_ERROR_EVENT` has publishers but no production subscriber, so it does not mitigate physical output. Health-monitor startup is disabled; when enabled, it registers only its task with a five-second task watchdog and does not directly inhibit the heater. No general supervisor/safe-mode recovery was found.

## Build, flash, and test

Standard initialized ESP-IDF shell:

```sh
idf.py build
idf.py -p PORT flash monitor
```

Local ESP-IDF 5.5.4 fallback used successfully when the `idf.py` Python environment was inconsistent:

```sh
IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake -S . -B build -G Ninja -DPYTHON=/home/vesko/.espressif/tools/python/v5.5.4/venv/bin/python3 -DPYTHON_DEPS_CHECKED=1
IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake --build build -j2
```

The pulled branch built successfully at `16467e0` with the local resolved `sdkconfig`: application `0x614f0` bytes (62% free of the smallest 1 MiB app partition). That configuration retains overshoot 30 C, stall check 180 s, PID Kd 0, and fan thresholds 35/32 C, while the changed Kconfig defaults specify 20 C, 300 s, Kd 0.030, and 43/40 C. Feedforward and the service operational-time override are disabled in that build. A build therefore does not validate fresh-default behavior unless its resolved configuration is recorded and checked.

No project host test suite or ESP-IDF unit-test component was found. Flashing, device bench, powered-controller, and powered-furnace work require explicit authorization and a validation plan.

## Fast navigation

```sh
rg -n 'xTaskCreate|xQueue|xSemaphore|xEventGroup|esp_timer|event_manager_' components main
rg -n 'gpio_|uart_|modbus|nvs_|esp_task_wdt' components main
rg -n 'TODO|FIXME|XXX|abort|assert|ESP_ERROR_CHECK' components main
rg -n 'malloc|calloc|realloc|free|strcpy|sprintf|memcpy' components main
```

Historical `CODE_ANALYSIS_REPORT.md` and `MODBUS_STUDY_PLAN.md` are leads only. Current findings are in `docs/analysis/FIRMWARE_FINDINGS.md`.

# Furnace firmware repository map

Last source-mapped: 2026-07-15. Hardening-closeout review: `bc743ab` (feature baseline `6741c72`). Source and resolved build configuration are authoritative; update both this file and `repository-map.yaml` after architectural changes.

Baseline/navigation refresh: 2026-09-14 at `1fcb4b1`; full subsystem revalidation
is pending. See the [baseline](../analysis/ANALYSIS-BASELINE.md) and complete
[component coverage index](../analysis/ANALYSIS-COVERAGE.md). The immediate
target is the central ESP32 with an external HMI protocol; the revision-two
STM32 hardware contracts below remain later work.

## Repository shape

| Path | Purpose |
| --- | --- |
| `main/` | `app_main()` entry point and top-level component dependencies |
| `components/` | ESP-IDF components: control, devices, communications, UI, drivers, and operations |
| `components/*/include/` | Public component interfaces |
| `components/*/src/` | Private implementation and state |
| `docs/codex/` | Agent-efficient architecture, standards, and Codex compatibility context |
| `docs/analysis/` | Evidence-backed findings; historical reports remain leads |
| `docs/architecture/` | Revision-two target contracts; explicitly not current implementation |
| `docs/templates/` | Bug, feature, ADR, and validation artifacts |
| `.codex/agents/` | Discoverable project agent definitions |
| `.agents/skills/` | Discoverable project workflow skills |
| `tools/codex/` | Repository setup verification tooling |
| `build/`, `managed_components/`, `sdkconfig`, `sdkconfig.old` | Generated/ignored local ESP-IDF state; not proof of deployed firmware |
| `sdkconfig.defaults`, `dependencies.lock`, `partitions.csv` | Tracked configuration/dependency inputs; the custom partition file is currently unselected |

Platform: ESP-IDF C, FreeRTOS, ESP32, private `esp_event` loop. Root build entry: `CMakeLists.txt`; application entry: `main/main.c:app_main`.

Deployment boundary (user confirmation, 2026-09-15): the operating oven has no
independent hardware safety protection; ESP32 checks are not certified safety
functions. GPIO source requests below are not measured electrical states.
Field settings reportedly match `field-tests`, which has no tracked resolved
`sdkconfig`; flashed source/configuration remain unverified.

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
10. Fan and enabled Nextion HMI initialization — void/unverified; run indicator is disabled and has no current startup call.
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
| Command dispatcher | `commands_dispatcher_task.c:commands_dispatcher_task` | Queue receive, two-second timeout; re-entrant submissions execute inline | Sole consumer; coordinator stop performs no dispatcher submission, removing the confirmed F-055 queue-full join cycle; broader external-producer shutdown remains open |
| Heater PWM task | `heater_controller_task.c:init_heater_controller_task` | Five-second default SSR window; task notification interrupts wait | Mutex-protected target; separate permanent SSR-failure and recoverable sensor-data inhibits |
| Coordinator control task | `coordinator_component_heater_controller.c:start_heating_profile` | PID timer plus sensor-expiry one-shot wakeups; pauses on invalid/expired aggregate | Profile, PID, mutex-protected temperature, recovery counter, and direct sensor-data inhibit; expiry timer enforces aggregate freshness (F-054) |
| Device manager task | `device_manager_task.c:init_device_manager_task` | Periodic Modbus updates and notification-shortened wait | Device table/state/lifecycle |
| Temperature processor task | `temperature_processor_task.c:init_temp_processor_task` | Device-update notification | Sensor array, sample buffer, context lifetime; normal shutdown destroys owned sensor devices after worker exit (F-056); device-manager mutation synchronization still needs coverage |
| HMI coordinator task | `hmi_coordinator.c:hmi_coordinator_init` | Queue receive every 50 ms | Sole display command/deferred-state owner |
| Nextion RX task | `nextion_rx_task.c:nextion_rx_task_start` | UART polling/read | UART mutex and transfer-active flags |
| Run-indicator task (inactive) | `run_indicator.c:run_indicator_init` has no current startup caller; config disables it | 200 ms polling/blink only if explicitly enabled and initialized | Registered/compiled archive, but no extracted runtime object in the stored map; synchronization remains a re-enablement prerequisite |
| Fan callbacks | `fan_controller.c:fan_controller_init` | Serialized event-loop callbacks | Static fan mode/state |
| Optional/inactive | health monitor, legacy SPI temperature monitor, transmitter diagnostics | Not started by current `app_main`; the legacy SPI temperature monitor is excluded from ESP-IDF discovery and the production dependency graph | Legacy SPI code is retained for reference but is not compiled into the production image |

No application hardware ISR registration was found. The PID timer callback uses `ESP_TIMER_TASK`, despite an ISR-context comment; the sensor-expiry callback also uses `ESP_TIMER_TASK`. UART drivers have internal interrupts; application code reads from tasks.

## Communication and synchronization inventory

| Primitive | Owner | Producers / readers | Consumer / writer | Failure/lifetime notes |
| --- | --- | --- | --- | --- |
| Private ESP event queue | `event_manager` | All event publishers | Private loop callbacks | Blocking wrapper can wait forever; heater `toggle_heater` posts after GPIO and can defer contactor-off (F-075); unsubscribe before context destruction |
| Dispatcher `command_queue` | `commands_dispatcher` | HMI, coordinator, other callers | Sole dispatcher task | External submissions use `portMAX_DELAY`; dispatcher-task self-submissions execute inline; coordinator self-exit skips final queue submissions so F-055's confirmed join cycle is removed |
| Logger queue | `logger_component` | Logging macros/callers | Logger task | 100 ms producer timeout then drop; no shutdown path |
| HMI command queue | `nextion_hmi` coordinator | Event bridges, RX/UI | HMI coordinator task | Lifecycle/error bridges prioritize critical commands with bounded wait and logging; telemetry remains best-effort and transfer buffer is bounded |
| Heater `power_mutex` | `heater_controller` | Dispatcher handler/task | Target power/state access | Does not protect all lifecycle fields |
| GPIO mutex | `gpio_master_driver` | Heater and callers using wrapper | Serialized GPIO calls | Does not prevent two components claiming one pin |
| SPI mutex | `spi_master_component` | Legacy SPI clients | Serialized SPI bus | Legacy path inactive |
| Logger storage mutex | `logger_component` | Logger/storage/CLI paths | RAM ring/files | Shutdown callback assumptions remain |
| Nextion UART recursive mutex | `nextion_hmi` transport | RX, send, storage, file reader | Serialized UART access | Transfer-active volatile flags are not locks |
| Program-model recursive mutex | Nextion program model | HMI handlers/storage | Draft/program accessors | Several scalar preferences remain outside mutex |
| Legacy ring mutex/event group | `temperature_monitor_component` | Legacy task/processors | Legacy only | Excluded from production discovery/compilation; not allocated by current startup |
| Binary worker-exit semaphores | dispatcher, coordinator, heater, device manager, temperature processor | Respective worker signals exit | Component owner waits before teardown | A worker acknowledgement does not quiesce external producers/callbacks |
| Task notifications | heater/coordinator/device/temp/health | Timers, stop functions, callbacks | Respective task | Notification is wakeup, not a complete join protocol |

The five worker-exit semaphores are created with `xSemaphoreCreateBinary` in
their component core files. Broader synchronization/lifetime correctness remains
the concurrency-review workstream.

## Major modules

| Module | Responsibility / public interface | Owned state and execution | Dependencies / hardware | Safety and coupling debt |
| --- | --- | --- | --- | --- |
| `common` | Shared program/task/error types and utility macros | Header-only; used in caller contexts | ESP event types and component consumers | Package C owns process-type review; D/E/F cover message, HMI/config, and utility coupling |
| `event_manager` + `event_registry` | Private event loop; `event_manager_*`; event bases/IDs | Global loop handle; event task callbacks | ESP event | Global singleton; payload/lifetime contracts are distributed |
| `commands_dispatcher` | Queued `command_t` routing; register/dispatch APIs | Queue, handler table, task, atomic running state, exit acknowledgement | FreeRTOS queue | Re-entrant handler submissions execute inline; external producer shutdown remains uncoordinated |
| `coordinator_component` | Program commands/status, control task, heater command production; sensor-data pause/recovery; boot/recovery temperature gate | Coordinator owns the active `run_program` copy; global profile/current temp/state/timers/task, temperature and manual-target mutexes, exit acknowledgement, recovery counter | Dispatcher, events, PID/profile, heater inhibit API | One-shot timer inhibits on aggregate silence (F-054), but cached acquisitions can be restamped (F-071) and sensor pause cannot auto-resume (F-069); STOP frees profile before join (F-072), and pause/RESUME outcomes can misstate output fault (F-077/F-078) |
| `temperature_profile_controller` | `load_heating_profile`, `profile_tick`, profile state/status and eased heating setpoint/handover | File-static profile context and tick counter | Common profile types | Lifetime/tick ownership coupled to coordinator; downward cooling setpoint can false-trip generic overshoot guard (F-076); ease-out extends runtime ramp while legacy/UI graph remains linear |
| `pid_component` | `pid_controller_compute/reset/reset_for_setpoint/set_adaptive_enabled` | File-static integral/history/adaptive/feedforward flag | Kconfig, logger | Single implicit instance; rejects non-finite inputs/state/output by reset/zero demand; optional feedforward and adaptive clamp can both be enabled |
| `heater_controller_component` | Contactor/SSR commands and time-proportional output | Global context; `power_mutex` protects target demand and permanent, sensor, and temporary control inhibit reasons; PWM task acknowledges exit before teardown | GPIO master; contactor GPIO 22, SSR GPIO 21 defaults | Direct inhibits request SSR/contactor off but GPIO-off error does not latch here (F-077); admitted high write can follow off (F-074), telemetry can defer off (F-075), cooling gap is F-070; physical result needs bench validation |
| `device_manager` | Device abstraction, state, periodic updates | Global device array/task/atomic running state/count; count tracks successful reservations and destruction; worker exit acknowledgement | FreeRTOS, events | Stop waits for worker exit; API mutation and producer/event lifecycle remain open |
| `modbus_master` | ESP-Modbus RTU init/read/write helpers | ESP-Modbus master instance; shutdown clears handle and requests require an active instance | UART2 TX27/RX26/DE25, 9600 defaults | No application serialization contract documented; timeout fixed at 300 ms; lifecycle validation remains open |
| `temp_sensor_device` | MS9024 abstraction and cached temperature | Static context pool; failed creation rolls back allocation; per-device cache and successful-read tick; full-register write/readback verification | Modbus registers PV 728, ID 127 | Per-device metadata feeds processor freshness, but device-manager writes and processor reads the cache without a coherent cross-task snapshot (F-060); device lifecycle remains cross-task |
| `temperature_processor_component` | Read fresh sensor samples, compact accepted samples, average/warning checks, publish legacy temperature plus validity snapshot | Global context, sensor array/ticks, compact buffer, task with exit acknowledgement; startup cleanup for partial sensor creation; owns active sensor-count and delta-warning Kconfig settings | Device events, furnace events | Coordinator expiry covers total update silence (F-054), but processor restamps accepted cached acquisitions at processing time (F-071); shutdown destroys owned sensors after worker exit (F-056), while manager mutation synchronization remains open |
| `temperature_monitor_component` | Legacy MAX31865/SPI path retained for reference | Not a production dependency; global context, task, ring/event group remain dormant | SPI/MAX31865 | Intentionally not compiled into production; revival requires an explicit architecture/PCB decision |
| `nextion_hmi` | UART protocol, UI events, HMI draft/program model, panel storage | `CONFIG_NEXTION_HMI_ENABLED` gates RX/coordinator tasks, queues and UART/program mutexes; HMI `s_run_program` getter has no observed writer, while coordinator owns the active run copy; registry, complete-read/atomic-draft load, bounded NAK and temporary-file save paths | UART1 TX32/RX33, panel SD/FileStream, NVS | HMI-local run flags and service-time accounting need externalization; bounded critical-event loss and panel-storage divergence remain; factory-reset delete-all exceeds task stack (F-073) |
| `fan_controller_component` | Auto/program fan policy | Event-loop-owned static state | Fan GPIO19 | No airflow feedback/interlock; program fan intent split from coordinator fields |
| `run_indicator` | Optional indication; disabled and not initialized | Task and event-written mode only if explicitly enabled and initialized | Archive remains a build/link input; stored link map has no extracted object; no current GPIO use | Re-enablement requires mailbox synchronization, schematic-approved pin, and hardware validation |
| `logger_component` | Async ESP logging, LittleFS/RTC crash records, CLI | Queue/task, ring/files/mutex | Core 1, LittleFS `/crash_dumps`, RTC memory | Drops under pressure; furnace-error events not integrated |
| `error_manager` | Error descriptor lookup | Static descriptor table | Common errors | No descriptor registrations found; not an active mitigation path; unified furnace-error mitigation is deferred to the future fault manager |
| `health_monitor` | Heartbeat table and task watchdog | Event table, health task | ESP task WDT | Disabled; watchdog/startup ordering and direct heater mitigation are deferred to the future fault-manager architecture |
| `gpio_master_driver` | Serialized GPIO setup/set | Global mutex | ESP GPIO | Pin ownership is not validated across components |
| `spi_master_component` | Serialized SPI wrapper | Global bus/mutex | ESP SPI | Primarily legacy/inactive |
| `heating_program_validation` | Program bounds/shape validation | Pure validation | Common profile types | Confirm every entry path invokes it |
| `debug_console` | UART0 console/commands | Task/global state | UART0 | Shutdown lacks join; operational only |
| `transmitter_diagnostics` | Retained diagnostic dump APIs | No active startup caller; dormant archive in stored link | Modbus/UART | Diagnostic load/lifecycle needs review before activation |
| `time_component` | Configuration-only build support | No C source, public API, or task implementation | CMake declares NVS/ESP timer dependencies | Existing task configuration does not establish a functioning time service |

## End-to-end flows

### Temperature acquisition

`device_manager_task` → `temp_sensor_update` → successful-read tick/cache → `DEVICE_MANAGER_UPDATED_EVENT` → temperature task notification → fresh-sample filtering/quorum → legacy float plus validity `TEMP_PROCESSOR_EVENT` → coordinator re-arms aggregate-expiry timer → direct inhibit/pause/recovery and fan/HMI telemetry. Silence after a valid aggregate now expires through the coordinator timer (F-054); regression and powered validation remain pending.

Key debt: physical-read success, cache age, validity, contributing sensor identity, and batch quorum are not carried with the published value.

### Profile and control

Nextion run handler → coordinator command in dispatcher queue → `start_heating_profile` resets PID, loads profile, queues heater clear/start, creates control task and periodic timer → timer notifies control task → `profile_tick` calculates phase/eased heating setpoint and handover → PID computes 0..1 demand (optional feedforward) → coordinator queues heater set-power → dispatcher invokes heater handler → PWM task windows SSR output. Sustained hold deviation or a ramp-stall condition posts enriched error data and pauses through the same queued `kill_heater` path.

Received sensor invalidity and one-shot aggregate expiry call the heater component's direct recoverable inhibit, which forces SSR/contactor off before coordinator pause; stop uses the same queue-independent direct inhibit (F-055). F-003 remains open.

### Physical outputs

- Contactor: heater commands → `start_heater`/`stop_heater` → GPIO 22 default.
- SSR: target power → heater PWM task → `toggle_heater` → GPIO 21 default.
- Fan: temperature/coordinator event callbacks → GPIO 19 default.
- Run indicator: disabled in production; future test re-enablement would route coordinator event → synchronized mode → indicator task → approved GPIO.

### HMI and persistence

Nextion RX task parses lines → HMI command queue → coordinator task/handlers → dispatcher or model/storage APIs. System events bridge into the same queue for display updates; error payloads now carry temperature, setpoint, stage, and elapsed-fault context but may still be independently dropped. User preferences are stored in NVS namespace `user_prefs`; program files live on the Nextion panel SD through FileStream/twfile. Factory reset erases ESP NVS then restores operational time; the optional service-time override is bounded to uint32-safe hours and only replaces the loaded value after a successful NVS write, while remaining applied at every boot when enabled.

### Logging, faults, watchdog, recovery

Logger producers → bounded queue → pinned logger task → ESP log and LittleFS/RTC records. The tracked single-app partition selection lacks the logger's requested `storage` LittleFS label (F-068), so persistent storage is not a deployable claim until the partition/flash contract is resolved. `FURNACE_ERROR_EVENT` has publishers but no production subscriber, so it does not mitigate physical output. Health-monitor startup is disabled; when enabled, it registers only its task with a five-second task watchdog and does not directly inhibit the heater. No general supervisor/safe-mode recovery was found.

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

Historical audit evidence reports a successful build at `1fcb4b1` with local
resolved `sdkconfig`: application `0x63cb0` bytes (61% free of the smallest 1 MiB
app partition). The 2026-09-14 baseline inspected matching stored image/config
metadata but did not build firmware. `sdkconfig` is ignored local state;
tracked `sdkconfig.defaults` is currently byte-identical, including overshoot
30 C, stall check 180 s, PID Kd 0, fan thresholds 35/32 C, feedforward disabled,
and service-time override disabled. The full generated defaults remain a
configuration baseline rather than a concise reviewed profile; clean defaults-only
resolution and current toolchain/dependency readiness remain future evidence.

No project host test suite or ESP-IDF unit-test component was found; planned deterministic cases are in `docs/analysis/REGRESSION-AND-VALIDATION-BACKLOG.md`. Flashing, device bench, powered-controller, and powered-furnace work require explicit authorization and a validation plan.

## Fast navigation

```sh
rg -n 'xTaskCreate|xQueue|xSemaphore|xEventGroup|esp_timer|event_manager_' components main
rg -n 'gpio_|uart_|modbus|nvs_|esp_task_wdt' components main
rg -n 'TODO|FIXME|XXX|abort|assert|ESP_ERROR_CHECK' components main
rg -n 'malloc|calloc|realloc|free|strcpy|sprintf|memcpy' components main
```

Historical `CODE_ANALYSIS_REPORT.md` and `MODBUS_STUDY_PLAN.md` are leads only. Current findings are in `docs/analysis/FIRMWARE_FINDINGS.md`.

## Accepted target direction (not yet implemented)

[ADR-0003](../decisions/0003-stm32-actuator-authority.md) assigns future
physical actuator/interlock authority and zero-cross SSR windowing to an external
STM32 controller. The ESP32 remains PID/profile/process coordinator and will send
bounded percentage demand within an STM32-owned sequenced control session;
[ADR-0004](../decisions/0004-actuator-session-sequence-and-stop.md) defines
lease, replay, and unconditional STOP behavior. Production raw SSR ON/OFF timing
commands are excluded. Current ESP32 GPIO/window behavior remains authoritative
until the migration is implemented and validated.

The revision-two target is split into four non-implementation contracts:
[system boundaries](../architecture/BOARD-REV2-SYSTEM-ARCHITECTURE.md),
[component structure](../architecture/COMPONENT-CONTRACT-STANDARD.md),
[coordinator commands/state](../architecture/COORDINATOR-STATE-AND-COMMAND-MODEL.md),
and the [ESP32–STM32 actuator protocol](../architecture/ESP-STM32-ACTUATOR-PROTOCOL-REQUIREMENTS.md).
Unresolved fan, pause, reset, aggregation, configuration, OTA, and fault-record
decisions remain tracked in
[`TARGET-ARCHITECTURE-REQUIREMENTS.md`](../analysis/TARGET-ARCHITECTURE-REQUIREMENTS.md).

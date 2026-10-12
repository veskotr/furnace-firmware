# Current central ESP32 controller behavior: integrated draft

Status: source-characterization draft, 2026-09-15. Baseline:
`1fcb4b1115ce9560fe2c4944ebc868cb454e5ed5`; the
[source manifest](baseline/2026-09-14-source.sha256) still matches. This covers
output, temperature, and process-control reviews A–C plus the external-HMI
boundary review E, operations/build review F, and concurrency review D.
Cross-component challenge and test design remain in progress.
No tests were run, firmware
changed, or hardware operated in this wave. Repository configuration is not
proof of the deployed image.

## Hardware and meaning of an output

The user confirmed the currently operating oven has **no independent hardware
safety protections**. ESP32 checks are operational, not certified safety
functions; separate hardware safety mechanisms are planned for production
([POL-HARDWARE-SAFETY-001](ESP32-ZEPHYR-PREPARATION.md)). Every output below is a
software-requested GPIO level, not measured relay/SSR state or heater power.
GPIO polarity, wiring, pre-initialization/reset states, electrical isolation,
and physical time-to-off remain unverified. No airflow feedback is present in
the reviewed source.

| ID / output | Resolved configuration | Current software request and context | Unknown or unclosed response |
| --- | --- | --- | --- |
| BHV-OUT-001 / contactor | GPIO 22 | [start/stop](../../components/heater_controller_component/src/heater_controller.c) write high/low from command, control, and inhibit contexts. | No contactor feedback; failed low write is not latched/retried. |
| BHV-OUT-002 / SSR | GPIO 21, 5,000 ms window | [PWM worker](../../components/heater_controller_component/src/heater_controller_task.c) drives high/low for 0–1 demand. | Authorization check and high write are not atomic; event publication can block after a write. |
| BHV-OUT-003 / fan | GPIO 19 | [fan callback](../../components/fan_controller_component/src/fan_controller.c) requests high at ≥35 °C, low below 32 °C; profile events hold it on. | GPIO/subscription errors ignored; no airflow proof or heater authorization dependency. |
| BHV-OUT-004 / indicator | Disabled | Current main does not initialize the [run indicator](../../components/run_indicator/src/run_indicator.c). | F-016 is a prerequisite if re-enabled; no current indicator-output claim. |

## Current operating-state and output-request matrix

| ID / transition | Current path | Heater request | Fan request / qualification |
| --- | --- | --- | --- |
| BHV-STATE-001 / boot and partial init | [main](../../main/main.c), [heater init](../../components/heater_controller_component/src/heater_controller_core.c), [coordinator init](../../components/coordinator_component/src/coordinator_core.c) | Eventually low contactor/SSR and initial sensor inhibit; state before pin setup or failed setup is unproven (F-057/F-059). | Fan auto once initialized; independent protections absent. |
| BHV-STATE-002 / start and heating/hold | [start](../../components/coordinator_component/src/coordinator_component_heater_controller.c), [handler](../../components/coordinator_component/src/coordinator_component_events.c) | Requires accepted float and valid aggregate lease, releases control inhibit, queues heater CLEAR/START; PID submits 0–1 demand to SSR PWM. | Fan held on by profile event, not airflow feedback. |
| BHV-STATE-003 / operator pause or recoverable profile fault | [pause](../../components/coordinator_component/src/coordinator_component_heater_controller.c) | Sets control inhibit, requests SSR/contactor low and demand clear, resets PID. | Fan stays in profile mode; fault reason is not represented as a separate pause owner. |
| BHV-STATE-004 / invalid or silent sensor input | [validity/expiry](../../components/coordinator_component/src/coordinator_component_events.c) | Sets sensor inhibit directly and requests SSR/contactor low before pause bookkeeping; pause also sets control inhibit. | No independent sensor-loss cutout. |
| BHV-STATE-005 / sensor recovery | [recovery callback](../../components/coordinator_component/src/coordinator_component_events.c) | After three valid aggregate *events*, releases sensor inhibit; an ordinary sensor-paused run stays paused because control inhibit still blocks its auto-resume check (F-069). | Cached measurements can earn those three events (F-071). |
| BHV-STATE-006 / cooling or cooldown | [phase handling](../../components/coordinator_component/src/coordinator_component_heater_controller.c) | Ordinary stage change queues CLEAR/zero; contactor stays high. A manual live phase change can omit the off request and leave positive PWM demand (F-070). | Coordinator `fan_on` flag is status/TODO; fan remains event-driven. |
| BHV-STATE-007 / STOP, completion, overshoot, reset request | [control](../../components/coordinator_component/src/coordinator_component_heater_controller.c), [Nextion reset](../../components/nextion_hmi/src/events/nextion_settings_handlers.c) | Direct control inhibit requests SSR/contactor low; STOP worker wake/join has F-061 and destroy-before-join F-072. | STOP/completion return fan to auto hysteresis using last float, which may be stale. |

The [heater inhibit predicate](../../components/heater_controller_component/src/heater_controller_task.c)
combines permanent hardware-output fault, sensor-data, and control reasons.
Current high-write paths test that predicate under the power mutex but release
the mutex before the GPIO write. A previously admitted high write can follow a
completed low-write inhibit (F-074). Blocking telemetry after an SSR write
can defer contactor-low in an inhibit (F-075). Direct off-write errors on
sensor/control inhibits are not latched and pause/STOP can still report
success (F-077). A permanent SSR latch can conversely remain active while
RESUME reports success (F-078). Error publication is not a physical
mitigation: the active firmware has no production furnace-error subscriber,
and the health-monitor startup call is disabled.

## Temperature-to-control contract

| ID / step | Current source behavior | Consequence / review link |
| --- | --- | --- |
| BHV-TEMP-001 / acquisition | [MS9024 PV](../../components/temp_sensor_device/src/ms9024.c) read uses register 728, CDAB float conversion and an assumed Celsius unit; [Modbus wrapper](../../components/modbus_master/src/modbus_master_core.c) has a 300 ms response timeout and no application retry per read. | Transmitter setup/unit identity and field device configuration are unverified. |
| BHV-TEMP-002 / cache and scan | [sensor cache](../../components/temp_sensor_device/src/temp_sensor_device_core.c) stores value and successful-read tick; failed read keeps both. [Manager](../../components/device_manager/src/device_manager_task.c) emits scan-complete even if reads fail. | Cache value/tick/flag has a cross-task race (F-060); destruction/update overlap is F-063. |
| BHV-TEMP-003 / aggregate | [processor](../../components/temperature_processor_component/src/temperature_processor_task.c) accepts caches aged ≤5 s, applies `max(1, sensor_count-2)` fresh-count quorum, averages, and stamps aggregate tick with processing time. | Source does not preserve measurement time in the aggregate; failed polls can renew its apparent age (F-071). |
| BHV-TEMP-004 / coordinator lease | [validity handler](../../components/coordinator_component/src/coordinator_component_events.c) accepts valid aggregate with aggregate-tick age ≤5 s, arms a one-shot expiry timer, and tracks three recovery events. Float value is a separate event. | Physical sample can approach 10 s old while control lease remains accepted; three events need not be three distinct reads (F-071). |
| BHV-TEMP-005 / zero and bounds | [float handler](../../components/coordinator_component/src/coordinator_component_events.c) compares to `-0.0f`, rejecting both signed zero values; [processor init](../../components/temperature_processor_component/src/temperature_processor_core.c) does not bound public count before indexing fixed arrays. | `B-CAND-ZERO` is a value/metadata mismatch; `B-CAND-COUNT` is conditional because main passes five. |

The current Modbus scan has a fixed 100 ms per-device delay in addition to
transaction time. Application-level worst-case scan/recovery timing and maximum
sensor-count policy still require package B/D/F synthesis and deployed-device
evidence. The bundled RTU stack checks CRC/address/function in normal receive
flow, but a CRC-valid forged short holding response has a separate library-test
question; this is not evidence of a real transmitter fault.

## Profile/control contract and unresolved candidates

The resolved baseline uses a 1,000 ms control timer, PID gains Kp 0.115,
Ki 0.001, Kd 0, 0–1 output, and a 5,000 ms SSR window. PID rejects non-finite
input/invalid `dt`, resets at start/pause/stop, and uses a measured worker-wake
interval, not nominal timer cadence. [Runtime profile](../../components/temperature_profile_controller/src/temperature_profile_core.c)
advances heat at actual ≥ target−0.5 °C, hold by elapsed dwell, cooling at
actual ≤ target+2 °C, and final cooldown below 40 °C.

F-076 records the validated >30 °C downward cooling step that can trip the
generic overshoot guard. Package C recorded these further source candidates
for exact host vectors and deduplication: `C-EASE-EST` (initial displayed heating duration
omits runtime ease-band extension), `C-MANUAL-RATE` (live manual ramp duration
omits easing extension and can exceed the requested initial rate), and
`C-DT-EXP` (sensor-expiry early `continue` can let the next profile/PID `dt`
include fault downtime). Cooling authorization, rather than graph/display
parity, is the first correction priority. No cure-performance limit was
supplied, so a mathematical trajectory test is not tuning or chamber evidence.

## Current HMI/domain boundary and later external operations

The enabled [Nextion HMI](../../components/nextion_hmi/src/nextion_hmi.c) has
an RX task and an HMI coordinator worker. Panel input travels through a
bounded HMI queue, [line router](../../components/nextion_hmi/src/events/nextion_events.c),
by-value dispatcher command, and coordinator handler. Queue admission is not
the same as command execution; a local display flag is not authoritative
process state. The later external HMI needs a controller-side control/status
protocol, but no transport or wire format is chosen in this analysis.

| ID / current operation | Source-owned behavior | External-HMI boundary requirement or open choice |
| --- | --- | --- |
| BHV-HMI-001 / run intent | [Run handlers](../../components/nextion_hmi/src/events/nextion_run_handlers.c) choose START/PAUSE/RESUME/STOP from HMI-local active/paused flags and submit to the coordinator via dispatcher. | Controller-derived state preconditions and distinct accepted/applied/rejected outcomes; STOP must not depend on a fresh display event. |
| BHV-HMI-002 / program draft and run | [Program handlers](../../components/nextion_hmi/src/events/nextion_program_handlers.c) validate saving against HMI ambient preference; run uses a different relaxed validator. Coordinator copies a submitted program into its run context. | User confirmed controller-owned program management/validation and immutable active snapshot; library storage and legacy import format still open. |
| BHV-HMI-003 / manual mode | [Manual handlers](../../components/nextion_hmi/src/events/nextion_manual_handlers.c) replace the shared draft with a seven-day one-stage program; live target/rate update uses a latest-value-wins coordinator mailbox. | Manual as explicit controller operation, unit/rate bounds and timeout; live cooling transition is F-070. |
| BHV-HMI-004 / settings and fan | [Settings handlers](../../components/nextion_hmi/src/events/nextion_settings_handlers.c) update RTC, NVS ambient/cooldown settings and a persisted Max/Silent fan preference; the preference does not command the actual fan. | Separate display clock from process settings; decide whether fan preference is excluded, presentation-only, or physical control. |
| BHV-HMI-005 / program files | [Panel storage](../../components/nextion_hmi/src/storage/nextion_storage.c) uses SD `.prg` files and a 200-name NVS registry; parser accepts legacy integer `delta=`. | Controller owns program management; versioned storage/transfer, compatibility/import and partial-write behavior still open. |
| BHV-HMI-006 / service time | [Run tick](../../components/nextion_hmi/src/events/nextion_run_handlers.c) adds one second per HMI worker interval ≥1 s while its local run flag is active, including pause, and flushes to NVS around minute increments. | Product semantics explicitly deferred; do not use current HMI counter as first-pass controller acceptance. |
| BHV-HMI-007 / reset | [Exact destructive commands](../../components/nextion_hmi/src/events/nextion_events.c) request direct heater inhibit before display/storage work and restart. Factory reset deletes registered panel files, erases NVS and attempts service-time restoration. Its delete-all frame exceeds the HMI worker stack (F-073). | Controller-side authorization, explicit partial-failure outcome and selected erased/preserved state; physical reset/off timing remains unverified. |

E recorded `E-CAND-STATE-001` (dropped lifecycle event can make HMI-local state
reject STOP or choose the wrong pause action; an F-014 residual),
`E-CAND-SERVICE-001` (worker stalls undercount operational time), and
`E-CAND-RESET-001` (failed panel deletes or NVS preservation can be ignored
during factory reset). These are source candidates for D/F cross-check and
product semantics; none selects a protocol. First-release transport, saved
program storage/import details, manual timeout, deferred service-hour definition, remote
reset availability, command retry identity, and reconnect snapshot freshness
remain open decisions.

## Operations, configuration, and memory evidence

| ID / source area | Current selected behavior | Limit or conditional source candidate |
| --- | --- | --- |
| BHV-OPS-001 / startup | [app_main](../../main/main.c) starts console/logger, event/dispatcher/heater/coordinator, then Modbus/fan/Nextion/device manager/five-sensor processor. Some early failures return; NVS, Modbus, device-manager and processor failures are logged and boot continues. | Startup success does not establish usable sensing or output state; F-057/F-059 partial cleanup and hardware pin state remain open. |
| BHV-OPS-002 / logger | [logger](../../components/logger_component/src/logger_core.c) requests a core-1 worker and 64-entry queue; producers wait ≤100 ms, then drop. | `F-CAND-LOG-001`: task-create failure ignored can leave initialized queue with no consumer. Logging is not heater mitigation. |
| BHV-OPS-003 / persistent records | [storage](../../components/logger_component/src/logger_storage.c) attempts LittleFS label `storage`, RAM ring, RTC snapshot and an orderly software-restart hook. | Selected 2 MiB single-app table lacks `storage` (F-068). `F-CAND-LOG-002` corrupt-header read overflow, `-003` panic-hook mismatch, and `-004` failed recovery clearing RTC are conditional on mounted storage. |
| BHV-OPS-004 / SDK guards | Resolved config enables boot/interrupt/idle-task watchdog options and brownout; app [health monitor](../../components/health_monitor/src/health_monitor_core.c) is not initialized. | No app error subscriber or watchdog path directly proves heater off; independent hardware safety is absent today. |
| BHV-OPS-005 / active memory | Stored map has two 8,800-byte logger buffers plus 1,780-byte RTC snapshot; 64×72-byte log queue payload is ≈4.5 KiB before metadata, 4,096-byte worker stack. [HMI reset delete-all](../../components/nextion_hmi/src/storage/nextion_storage.c) compiled frame 12,896 bytes exceeds its 8,192-byte stack (F-073). | Configured/static/artifact budgets only; no heap free, task-stack high-water, or deployed image measurements. PSRAM/coredumps off, canaries on. |
| BHV-OPS-006 / dormant/support | `error_manager`, health monitor, diagnostics, SPI wrapper are dormant link inputs; `time_component` has no implementation; root excludes legacy MAX31865 monitor. | Activation requires separate lifecycle/safety review; excluded SPI error path can retain mutex (`F-CAND-SPI-001`). |

The user reports field settings match ancestor `field-tests`, but that branch
tracks no resolved `sdkconfig`; HEAD differs from it in 51 application/build
paths. [Baseline provenance](ANALYSIS-BASELINE.md) separates repository
configuration/source evidence from an unverified flashed image. Future
production hardware protections are planned; none may be credited today.

## Proposed acceptance scenarios, not executed tests

| ID | Current-characterization / intended-correction oracle | Lowest useful level and remaining evidence |
| --- | --- | --- |
| SCN-W1-001 | Positive manual demand, live target drops >2 °C below current: characterize no current off request; corrected cooling must revoke demand and request SSR/contactor low before another high. | Host profile vector plus mocked coordinator/dispatcher/GPIO call-order test; powered output behavior remains open. |
| SCN-W1-002 | Valid run, invalid aggregate or silence, then qualifying new acquisitions (three for a test vector using the current setting): characterize sensor gate release but paused state remains; corrected run auto-resumes *only* its sensor-induced pause, not operator/fault/permanent reasons. | Mock event/timer/heater with fake monotonic time and pause-reason trace; exact product qualification and physical off/on timing remain open. |
| SCN-W1-003 | Barrier an in-flight PWM/START high write after its inhibit check, complete direct low writes, then release it: final software request must stay low after correction. | Mocked GPIO and task barriers; no scheduler sleeps, hardware polarity/relay feedback still required. |
| SCN-W1-004 | Failed scans after one good read: physical acquisition tick must not renew; recovery credit must not advance without the selected new-acquisition policy. | Host/mock device cache→aggregate→lease with fixed ticks; field transmitter timing and selected quorum remain open. |
| SCN-W1-005 | Full/stalled event queue when PWM is high or an inhibit fires: output-off requests must not wait for telemetry and must complete within a defined software deadline. | Mocked event queue and GPIO order; a physical deadline needs a separately authorized bench plan. |
| SCN-W1-006 | Valid 150→100 °C cooling transition: do not emergency-complete solely because actual exceeds the *lower* target; retain a separately specified high-temperature fault response. | Host profile vectors; product overtemperature ceiling and powered behavior still open. |
| SCN-E-001–003 | Distinguish START admitted/applied/rejected and retry, accept STOP/PAUSE from controller state after lost HMI events, and keep the active run copy stable during draft edit/HMI loss. | Host command-state/parser tests and mocked queue/persistence integration; link loss and panel storage remain hardware evidence. |
| SCN-E-004–005 | Validate manual target/rate including F-070 cooling direction; disconnect/reconnect HMI while a healthy run continues with no implicit START/RESUME authority. | Host vectors and mocked controller/protocol tests; actual link behavior remains open. |
| SCN-E-006–007 | Deferred service-time vectors: fake monotonic time across pause/HMI stall once product semantics are selected. Separately inject partial program delete/save/NVS failures and return explicit reset outcomes. | Mocked clock/storage/command integration; service hours are not a first-pass acceptance gate; panel and reset electrical behavior remain open. |
| SCN-D-001 | Preempt worker after `profile_tick` singleton check, dispatch STOP, then release: profile/PID destruction must follow worker exit acknowledgement, with direct off before join and no late high command. | Mocked worker/dispatcher/profile lifetime barriers; physical output and crash/reset behavior remain open. |
| SCN-F-001 | Compare compiled factory-reset delete-all frame to configured task stack; run mocked empty/full registry and panel-delete failure vectors with bounded storage. | Static/frame-size budget plus mocked reset/storage, never on the operating oven; physical off/reset behavior open. |
| SCN-W3-001 | Persistent contactor-low error during sensor invalidity and pause (or one STOP off-error), then qualified recovery and RESUME/START: incomplete mitigation must persist as a blocking fault with truthful command result. | Mock GPIO/heater/coordinator/event order; actual contactor state and fault detection remain hardware gaps. |
| SCN-W3-002 | Permanent SSR fault from PWM/CLEAR, then PAUSE and RESUME: do not publish RESUMED or clear fault/paused run state while the output gate remains permanently inhibited. | Mock heater fault, coordinator command/result/event; no hardware heating claim. |

D's [ownership and interleaving table](AUDIT-006-dispatcher-device-lifecycle.md)
reconciles F-072/F-074/F-075 and leaves conditional sensor-gate generation,
timer task-handle, handler-argument, and GET_STATUS snapshot candidates for
focused tests. Cross-component challenge may supersede this
partial trace where warranted. The integrated correction/Zephyr plan is not yet
authorized by this source characterization.

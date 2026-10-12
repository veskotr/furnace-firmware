# Audit 007: Operations, diagnostics, and dormant monitoring

- **Date:** 2026-08-04
- **Commit:** `1fcb4b1` (`audit-docs-verification`)
- **Scope:** source-only map of fan, logger/crash records, error descriptors, health/watchdog, UART0 console, transmitter diagnostics, time placeholder, and legacy SPI temperature monitor.
- **Hardware state:** not operated. No diagnostic scan, GPIO, watchdog, or restart action was performed.

## Active-path map

| Area | Startup / execution context | Source-backed behavior | Boundary / limitation |
| --- | --- | --- | --- |
| Debug console | `app_main` calls `debug_console_init` before NVS/logger; a UART0 task blocks in `linenoise`. | Registers `help`; later logger initialization registers `log-ls` and `log-file`. Init failure is logged and boot continues. | Operational interface only; it neither owns nor mitigates heater state. Its public shutdown protocol is unsafe (proposed F-063). |
| Logger and restart records | `logger_init` creates a queue and requests a core-1 task, then attempts CLI and LittleFS `/crash_dumps`; a shutdown handler is registered only after storage mount succeeds. | Producers wait up to 100 ms, then drop; the worker emits ESP logs and RAM-ring entries. The handler runs on orderly `esp_restart`, not the normal panic restart path. | Selected partition lacks `storage` (F-068), so mount/handler registration fail and logger still marks initialized. No `FURNACE_ERROR_EVENT` subscriber calls `logger_store_full_log`; records are observational, not physical mitigation. |
| Error manager | Linked by coordinator; no startup routine or task. | A static descriptor table maps a source ID to description text when a caller explicitly registers one. | `rg` found no descriptor registration or call to `get_error_description`; this does not receive furnace events or mitigate outputs. |
| Fan controller | `app_main` calls `fan_controller_init` after Modbus init; its callbacks run serially on the private event loop. | Configures GPIO 19 low, turns on at configured auto threshold, off below the lower threshold, and holds on during running/resumed profiles. After stop/completion it resumes auto policy from the last float temperature event. | GPIO return values and event-subscription errors are discarded; there is no airflow feedback, fan fault detection, output ownership guard, or heater interlock. This is a design/validation gap, not a proven defect. |

`FURNACE_ERROR_EVENT` publishers exist in heater, coordinator, device manager, and temperature processor. No production subscription was found. Therefore errors, error descriptors, logger records, console output, and health events must not be presented as physical mitigation. The direct heater inhibit paths remain the relevant source-backed output mitigation.

## Inactive or dormant paths

| Area | Current production status | Source-backed behavior and constraint |
| --- | --- | --- |
| Health monitor / task watchdog | `main.c` comments out `init_health_monitor`, although the component is linked. | If enabled, an event-loop callback registers/updates heartbeat table entries. Its task registers *itself* with a 5 s ESP task watchdog and withholds watchdog reset after missed heartbeat thresholds. It does not directly inhibit heater outputs. Do not enable it without a lifecycle correction and an approved fault/mitigation design. |
| Transmitter diagnostics | Linked, but `transmitter_diagnostics_auto_init` is commented out. | Manual/auto diagnostic code scans configured Modbus addresses and reads a broad register set, with 20--30 ms inter-read delays. It performs no writes. Activating it concurrently with normal device-manager traffic requires a lifecycle/load/diagnostic authorization and bench plan. |
| Time component | No C source or public include directory exists; `main` has no dependency or call. | The component CMake file glob is empty and only declares NVS/esp-timer dependencies. Logger timestamps are milliseconds since boot; fields intended for wall-clock/total-runtime values are currently written as zero. |
| Legacy SPI/MAX31865 temperature monitor | Top-level CMake excludes `temperature_monitor_component` from ESP-IDF discovery and `main` does not require it. | Retained reference code owns SPI, a task, event group, ring buffer, and error events, but is not part of the production image. Its source has independently broken init/teardown paths (proposed F-065/F-066); revival requires architecture, PCB, and safety decisions first. |

## Source-proven proposed findings

These identifiers are proposed for the shared finding register; this audit does not edit that register or production code.

### Proposed F-063 — Debug-console shutdown does not join its task before deinitializing console state

- **Confidence:** confirmed, dormant under normal boot-only operation; high lifecycle if its public shutdown/reinit API is used.
- **Evidence:** `debug_console_shutdown` only writes `debug_console_running = false` and immediately calls `esp_console_deinit`; it neither wakes/joins the task nor clears `debug_console_task_handle`. The task can be blocked in `linenoise`, and after returning it uses console/linenoise state then loops according to the same ordinary global flag. A rapid public `shutdown` then `init` can set the flag true before the old task checks it, leaving two tasks against reinitialized console state.
- **Reachable trigger:** call `debug_console_shutdown`, then `debug_console_init` before the first task exits/unblocks.
- **Impact:** use-after-deinit/concurrent console access, task duplication, and unsafe reinitialization. It does not directly command heater GPIO.
- **Direction:** define one owner, stop/wake mechanism, exit acknowledgement, and reverse-order console/UART teardown; do not use `volatile` as a join mechanism.

### Proposed F-064 — Health-monitor shutdown frees context while its worker can still access it

- **Confidence:** confirmed if the currently disabled component is enabled; high lifecycle/watchdog.
- **Evidence:** `shutdown_health_monitor_task` sets `ctx->is_running = false`, notifies the worker, and returns without waiting. `shutdown_health_monitor` then frees `g_health_monitor_ctx`. The worker dereferences `ctx` for loop exit, calls `esp_task_wdt_reset/delete`, writes `ctx->task_handle = NULL`, and only then deletes itself. `is_running` is also declared `volatile`, which does not establish a lifetime protocol.
- **Reachable trigger:** enable health monitor, then call `shutdown_health_monitor` while its task is in `ulTaskNotifyTake` or its scan.
- **Impact:** worker dereference of freed state; watchdog cleanup and future reinit become unreliable. The monitor has no direct heater-inhibit action, so physical-off behavior is not claimed.
- **Direction:** add worker exit acknowledgement and only free/unregister after producers and worker quiesce; separately decide whether any health failure may request a direct heater inhibit.

### Proposed F-065 — Health-monitor partial initialization retains subscribed/freed state

- **Confidence:** confirmed if enabled; high lifecycle.
- **Evidence:** `init_health_monitor` allocates and `init_health_monitor_events` subscribes using the context pointer, then `CHECK_ERR_LOG_RET(init_health_monitor_task(...))` returns directly on task/watchdog failure. It neither unsubscribes nor frees the context. `initialized` remains false, so a subsequent init overwrites the global pointer with a new allocation while the event loop retains the old handler argument.
- **Reachable trigger:** fault-inject task-watchdog or task creation failure after event subscription, then retry health-monitor initialization and publish `HEALTH_MONITOR_EVENT`.
- **Impact:** retained allocation and event callback to orphaned state; later cleanup cannot reach the original context. No heater control effect is claimed.
- **Direction:** track each acquisition independently and unwind in reverse order; test every failure boundary.

### Proposed F-066 — Legacy temperature monitor has double-free and no worker join in excluded code

- **Confidence:** confirmed in the explicitly excluded legacy component; not reachable in current production image.
- **Evidence:** on `init_temp_sensors` or task-start failure, `init_temp_monitor`’s cleanup expressions call `free(g_temp_monitor_ctx)` twice and do not reliably clear the global pointer or delete the event group/ring mutex. On normal shutdown, `stop_temperature_monitor_task` only clears/notifies; `shutdown_temp_monitor` immediately deletes the event group and frees context while the task later accesses `ctx` and clears its task handle.
- **Reachable trigger:** explicitly re-enable the component and fault-inject sensor/task initialization, or invoke its public shutdown during the task wait/processing cycle.
- **Impact:** heap corruption/use-after-free in a legacy sensing path. It is not a current-image physical-control finding.
- **Direction:** do not revive this component incrementally; first choose the temperature architecture/PCB ownership, then rebuild its init/stop protocol with one cleanup path and join.

## Validation gaps and improvement candidates

- Add host fault-injection tests for proposed F-063--F-066 before any activation/reuse. Oracles: no task survives shutdown, no registered handler remains, and retry returns to a clean baseline.
- Bench-validate fan GPIO polarity, fan start/stop behavior, threshold/hysteresis values from resolved configuration, and whether airflow must become a heater interlock. Compilation cannot establish any of these.
- Decide an authoritative fault manager before enabling watchdog enforcement: event severity, latching/recovery, owner of heater direct inhibit, HMI/logging behavior, and watchdog/reset order. Existing health monitoring is not that authority.
- Test logger queue saturation and LittleFS mount/full/corrupt-record behavior. Treat dropped logs and panic snapshots as forensic limitations, not safety controls.
- Before calling transmitter diagnostics in a deployed system, characterize serial-bus load and interaction with device-manager requests; execute only under an approved non-heating maintenance plan.
- Either remove/archive the empty time placeholder or define time-source, monotonic/wall-clock semantics, retention, and error policy. Do not infer operational time from the current zero fields.

## Recommended next slice

Map dispatcher/event-manager producer shutdown and device-table destruction with explicit event callback quiescence. It intersects proposed F-064/F-065 and determines whether component shutdown can be safely made reusable. No hardware action is authorized.

## Wave-2F source/configuration reconciliation (2026-09-15)

The unchanged-baseline F review confirms [F-068](FIRMWARE_FINDINGS.md) and
separates configured/static memory from measured headroom. The selected 2 MiB
single-app table has no LittleFS `storage` label; the unused
[partitions.csv](../../partitions.csv) describes 8 MiB. Stored map evidence
shows two 8,800-byte static logger entry buffers and a 1,780-byte RTC snapshot;
the configured 64×72-byte queue payload is about 4.5 KiB before queue
metadata, with a 4,096-byte task stack. No heap or stack high-water was
measured. PSRAM and coredumps are off; stack canaries are on.

Source candidates, conditional on their stated triggers, remain package-local
until test/fault injection and deduplication:

- `F-CAND-LOG-001`: fault-inject logger task creation failure after queue
  creation. `logger_init` ignores the result and marks initialized; producers
  fill a no-consumer queue, then wait up to 100 ms and drop each log.
- `F-CAND-LOG-002`: after a valid mounted storage partition is selected,
  `read_log` trusts a magic-only header. A corrupted `write_index>99` and a
  sufficiently long file make it `fread` more than its 100-entry static
  `read_buffer`; the selected F-068 configuration cannot execute this CLI
  filesystem path.
- `F-CAND-LOG-003`: the function named `panic_capture` is registered as an
  orderly software-shutdown handler only after LittleFS mount. Local IDF
  `esp_restart` invokes it; normal panic restart uses `esp_restart_noos`.
  Do not describe this as panic/WDT forensic capture.
- `F-CAND-LOG-004`: with mounted-but-failing storage, next-boot recovery logs
  a failed write then clears the RTC snapshot anyway, losing the retained
  record.
- `F-CAND-SPI-001`: excluded SPI monitoring would retain its mutex after a
  transmit error if re-enabled; it is dormant under current root exclusion.

Application health-monitor and transmitter-diagnostic startup calls remain
commented out. SDK idle-task/boot/interrupt watchdog and brownout settings
exist but are not an application heater-off protocol. The currently operating
oven has no independent hardware safety protections; ESP32 operational checks
are not certified safety functions. No build, mock, or powered test ran in this
review. See [current controller behavior](CURRENT-CONTROLLER-BEHAVIOR.md)
for the cross-component trace.

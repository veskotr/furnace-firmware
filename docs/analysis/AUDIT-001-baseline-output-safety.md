# Audit 001: Baseline and output-safety map

- **Date:** 2026-08-04
- **Commit:** `1fcb4b1` (`audit-docs-verification`)
- **Scope:** static map and build verification of startup plus heater/SSR/contactor output paths
- **Hardware state:** not operated

## Baseline evidence

`sdkconfig` and `sdkconfig.defaults` agree for the mapped output configuration: contactor GPIO 22, SSR GPIO 21, fan GPIO 19, Nextion enabled, and run indicator disabled. The project rebuilt successfully with ESP-IDF 5.5.4 using:

```sh
IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake --build build -j2
```

The generated application binary is `0x63cb0`; the smallest app partition is `0x100000`, leaving `0x9c350` (61%) free. CMake reported an offline component-registry check and a non-fatal ESP-ROM gdbinit-generation warning. This compile result does not validate GPIO polarity or physical output behavior.

## Source-mapped startup and outputs

| Area | Source ownership and path | Static result | Verification limit |
| --- | --- | --- | --- |
| Startup | `main/main.c:app_main` | Event manager, dispatcher, heater controller, and coordinator fail fast. Modbus, device manager, and temperature processor errors are logged and startup continues. | Whether a partially started system can receive an operator command after a later failure needs control-path review. |
| Contactor | `heater_controller.c:start_heater` / `stop_heater` | GPIO 22 is driven high for start and low for stop. Heater initialization configures pull-down and eventually calls stop. | Active polarity and external contactor wiring are unresolved hardware assumptions. |
| SSR | `heater_controller.c:toggle_heater`; `heater_controller_task.c:heater_controller_task` | GPIO 21 is driven high only during a PWM on-window and low afterward/on task exit. | SSR polarity, electrical isolation, and timing are unverified. |
| Direct inhibit | `heater_controller_task.c:heater_controller_set_sensor_data_inhibit_for_context` and `heater_controller_set_control_inhibit_for_context` | Inhibit clears demand then synchronously requests SSR-off and contactor-off without using the dispatcher queue. | GPIO-call failure behavior needs deterministic fault injection and powered validation. |
| GPIO failure | `heater_controller_task.c:heater_controller_handle_ssr_failure` | First SSR GPIO error latches the permanent inhibit, clears demand, retries SSR-off, and requests contactor-off. | F-017 remains pending hardware validation. |
| Command boundary | `heater_controller_events.c:heater_command_handler` | Heater start, positive toggle, and demand updates reject an inhibited output; clear directly drops SSR before clearing demand. | Queue ordering and all command producers are reviewed later under concurrency/control workstreams. |
| Disabled indicator | `sdkconfig{,.defaults}`, `main/main.c`, `run_indicator` | Run indicator is disabled in the resolved configuration and not initialized by the production startup gate. | Re-enablement requires its separate synchronization, pin, and powered validation work. |

## Safety invariants checked

Static source supports these claims: valid direct inhibits do not depend on the dispatcher queue; task exit asks SSR-off; and heater command paths reject reauthorization while an inhibit is set. The following cannot be claimed from source/build: safe pin state before application GPIO configuration, deployed polarity, contactor/SSR independence, actual time-to-off, behavior during reset/brownout/watchdog, airflow/interlock behavior, or the result of GPIO write failures.

## Findings and documentation changes

- Corrected a stale F-056 statement in both repository maps. Current `shutdown_temp_processor` joins the worker, calls `destroy_devices`, then frees its context.
- Added confirmed lifecycle finding [BUG-057-heater-partial-init-cleanup.md](BUG-057-heater-partial-init-cleanup.md): partial heater initialization bypasses its cleanup routine.
- Recorded P-009 for later review of component source-list declarations; no build behavior changed.

## Next verification slice

Trace the coordinator’s start/pause/stop/reset paths and temperature-validity gate into the heater API, then review those paths with the control and FreeRTOS workflows. Hardware validation remains planned only.

## Wave-1 source reconciliation (2026-09-15)

The A/C reviews completed the requested control-to-output trace at the unchanged
source baseline; [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md) records the
state/output request matrix. The operating oven has no independent hardware
protection, per the user; this audit's earlier uncertainty about external
interlocks must not be read as evidence one exists. Polarity, wiring, electrical
isolation, and physical off latency remain unverified.

- [F-070](BUG-070-cooling-output-authorization.md): a live manual transition
  to COOLING can leave a positive target and PWM running without any off
  request. Ordinary stage cooling queues CLEAR/zero rather than asserting a
  direct output inhibit; a full dispatcher queue can delay those requests.
- [F-074](BUG-074-heater-high-write-after-inhibit.md), formerly
  `A-CAND-GATE-002`: PWM and command handlers check inhibit under
  `power_mutex` but release it before a high GPIO write. An inhibit can finish
  its low writes between that check and a stale high write; GPIO's own mutex
  serializes writes individually, not authorization plus write. The earlier
  statement that inhibited commands are rejected remains true for checks
  *after* inhibition, not for an already admitted writer.
- [F-075](BUG-075-heater-telemetry-blocks-off.md), formerly
  `A-CAND-TELEM-003`: `toggle_heater` posts telemetry after an SSR write using
  `portMAX_DELAY`. A full private event queue can block a non-event-loop PWM
  task with SSR requested high, or block an inhibit between SSR-low and
  contactor-low. ESP-IDF same-loop posts use zero wait; no same-loop self-post
  deadlock is claimed.
- [F-077](BUG-077-direct-inhibit-off-failure.md), formerly the contactor
  portion of `A-CAND-AUX-005`: direct inhibit SSR/contactor-low errors do not
  latch an output fault; coordinator pause/STOP can report success and later
  normal recovery/RESUME can clear their reasons. Fan GPIO/subscription
  failures remain an unnumbered candidate; its software state can advance
  despite a failed write. Electrical consequences remain hardware-dependent.
- [F-069](BUG-069-sensor-recovery-pause-gate.md): three accepted aggregate events
  release only the sensor inhibit. The separate control inhibit asserted by
  the sensor pause prevents the advertised automatic run resume.
- [F-078](BUG-078-resume-under-permanent-output-fault.md): the opposite
  status mismatch also exists—explicit RESUME can report success/publish
  RESUMED while the permanent SSR output reason remains active and heat is
  still blocked. No re-energization is claimed from that trigger.

Package D independently challenged the output check/write and queue
interleavings, confirming F-074/F-075. No A–C/D source review is a
release or hardware-validation result.

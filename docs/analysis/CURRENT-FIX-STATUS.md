# Current firmware fix status

Date: 2026-07-14
Scope: source changes currently present on the working branch; no powered hardware validation claimed.

| Finding | Current source status | Evidence / validation | Still open |
| --- | --- | --- | --- |
| F-016 | Collision source fix implemented | Run indicator defaults off and refuses invalid/contactor-colliding GPIOs; current retained `GPIO22` configuration therefore claims no indicator GPIO | Build/repository verification; schematic-approved indicator pin and powered validation |
| F-003 | Narrow source fix implemented | Temporary heater-side control inhibit is asserted for pause/stop/completion/emergency paths and rejects stale power/start commands; released only on profile start/resume | Regression harness, queue-order characterization, and physical output-off validation; later generation protocol remains deferred |
| F-018 | Reset inhibit source fix implemented | Restart and confirmed factory-reset fail closed unless direct heater control inhibit succeeds before delays/storage/reset | GPIO polarity, electrical isolation, reset timing, and powered-controller validation |
| F-019 | Start-order source fix implemented | Coordinator task is created before heater inhibit release and HEATER_CLEAR/HEATER_START submission | Regression/fault-injection coverage; timer failure F-011 remains separate |
| F-048 | Profile easing math fix implemented | Final ramp band now starts at the configured rate, decelerates monotonically, and extends planned ramp time for the slower tail | Profile/PID regression characterization and real-world overshoot validation |
| F-022 | Complete-read and atomic-parse source fix implemented | Nextion file reads require the reported byte count and parsed programs replace the active draft only after the complete transfer | UART fault-injection/read regression coverage; panel storage validation |
| F-023 | Bounded-NAK source fix implemented | Repeated packet NAKs abort the transfer after three retries and release the UART/storage path | UART fault-injection and independent HMI stop-path validation |
| F-024 | Temporary-file replacement source fix implemented | Program saves transfer to a temporary file, replace the destination only after completion, and verify final contents | Panel rename/power-loss validation; atomic replacement is limited by panel storage semantics |
| F-025 | Full-register MS9024 verification source fix implemented | Write readback and auto-correct now compare the complete 16-bit register value | Modbus fault-injection and powered sensor validation |
| F-005 | Source fix already present; documentation reconciled | Temperature processor compacts successful fresh samples into the front of the aggregate buffer | Sparse-success regression coverage and sensor validation |
| F-010 | Initialization-failure source fix implemented | Temperature processor aborts startup and cleans up partial sensor creation instead of launching an incomplete worker | Initialization fault-injection and sensor lifecycle validation |
| F-021 | Source synchronization fix present | Temperature event writer and coordinator control reads use the mutex-protected snapshot/getter | Executable regression characterization; freshness and lifecycle risks remain separate |
| F-006 | Worker-ack source fix implemented | Temperature processor worker acknowledges exit before owner frees context; stop flag is atomic | Stop/restart regression coverage; broader producer/lifecycle validation |
| F-007 | Worker-ack source fix implemented | Dispatcher worker acknowledges exit before queue/handler/context cleanup; shutdown aborts cleanup if acknowledgement fails | Stop/restart regression coverage; blocked external producers remain open |
| F-008 | Coordinator worker-ack source fix implemented | Coordinator control worker acknowledges exit before owner destroys mutex/context; inactive/completed contexts are also cleaned up | Stop/restart regression coverage; event callback quiescence and broader producer lifecycle |
| F-009 | Heater worker-ack source fix implemented | Heater PWM worker turns the output off and acknowledges exit before the owner deletes its mutex/semaphore/context | Stop/restart regression coverage; physical output-off validation |
| F-012 | Device-manager worker-ack source fix implemented | Device-manager stop waits for the worker exit acknowledgement before a later init can reuse the context | Stop/restart regression coverage; Modbus producer/device lifecycle validation |
| F-001 | Phase B source fix implemented | Fresh-read ticks, fresh-sample quorum, direct recoverable heater inhibit, pause, and three-valid-aggregate recovery; see [CHANGE-VALIDATION-F001-PHASE-B.md](CHANGE-VALIDATION-F001-PHASE-B.md) | Regression/hardware validation; future per-board quorum/mapping |
| F-002 | Minimal source fix implemented | Dispatcher-task re-entrant submissions execute handlers inline; build and repository verification passed in [CHANGE-VALIDATION-F002.md](CHANGE-VALIDATION-F002.md) | Regression harness, nested stack characterization, teardown F-007, physical off-path validation |
| F-017 | Local source fix implemented | SSR failure latches heater-local inhibit, retries SSR-off, and directly requests contactor-off; see [CHANGE-VALIDATION-F17.md](CHANGE-VALIDATION-F17.md) | GPIO polarity/isolation and powered-controller validation; global fault model intentionally deferred |
| F-020 | Source mitigation present | PID state reset is performed on profile start, stage handover, and resume | Characterization and regression coverage |
| F-053 | Source fix implemented | Non-finite PID inputs/state/output and heater demand are rejected or forced to zero; see [CHANGE-VALIDATION-F53.md](CHANGE-VALIDATION-F53.md) | Executable regression coverage |

## Important clarification: F-001 Phase B

F-001 Phase B is now present in source under the current architecture. The approved contract is in [ADR-0002](../decisions/0002-recoverable-sensor-data-inhibit.md). The implementation uses a separate validity event so existing HMI/fan float-event consumers are unchanged.

## Common validation limits

- The production firmware build has passed with ESP-IDF 5.5.4.
- Repository-local Codex verification has passed.
- There is no automated firmware test harness for these paths yet.
- No powered-controller or powered-furnace validation has been performed.

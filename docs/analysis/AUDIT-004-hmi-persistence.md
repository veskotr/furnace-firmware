# Audit 004: Nextion HMI, commands, and persistence

- **Date:** 2026-08-04
- **Commit:** `1fcb4b1` (`audit-docs-verification`)
- **Scope:** enabled HMI initialization, UART/task/queue ownership, panel command routing, destructive actions, and persistence boundaries
- **Hardware state:** not operated

## Runtime map

With `CONFIG_NEXTION_HMI_ENABLED=y`, `nextion_hmi_init` initializes the program model, storage registry, UART, HMI coordinator queue/task, RX task, and initial display command. The RX task frames panel lines and posts `HMI_CMD_HANDLE_LINE` to the coordinator. The coordinator is the sole executor of panel command handlers, program-model work, and storage calls; event-loop bridges enqueue telemetry and profile/error updates for the same task.

| Boundary | Owner/contract | Source-backed behavior | Limit |
| --- | --- | --- | --- |
| UART | Recursive UART mutex plus storage/file-reader active flags | RX takes the mutex only around one-byte reads; storage/file-reader hold it for panel transfer/read sequences. | Active flags are coordination hints, not a general lifecycle protocol; panel behavior is unverified. |
| HMI queue | `hmi_coordinator_task` | Telemetry is best-effort; lifecycle/error events receive front-of-queue insertion and a bounded 100 ms wait. During transfers, latest temperature and up to eight critical events are deferred. | Bounded queue/deferred buffer can still lose critical display information, as tracked by F-014. |
| Panel-originated line | RX task → HMI queue | RX waits up to 100 ms to enqueue a complete line, then logs and drops it if full. | Command reliability is not classified by command type; a saturation trigger for a dropped STOP/RESET line is not yet characterized. |
| Destructive commands | Exact cleaned command routing | `restart`, `factory_reset`, and confirmation require `strcmp` equality; reset paths call direct heater control inhibit before UI/storage delay and restart. | F-018/F-032 require malformed-frame and powered reset validation. |
| Program persistence | HMI coordinator/task and panel storage | Complete read/atomic draft replacement, bounded NAK retries, temporary-file write, readback verification, and persistent program-name registry are present. | Final panel rename/power-loss window and out-of-registry files remain F-024/F-026 limits. |
| Program model/NVS | Recursive program-model mutex | HMI draft access uses the mutex; coordinator copies the submitted program into its own active `run_program`. The HMI `s_run_program` has a getter but no observed writer. Operational time attempts to persist through factory reset. | Lifecycle teardown is absent; configuration/power-loss behavior requires separate validation. |

## Safety and lifecycle results

The HMI does not directly drive heater GPIO. Run/pause/stop commands traverse the dispatcher/coordinator; reset paths instead invoke the heater’s direct inhibit first. Therefore UI telemetry loss must not be treated as a physical mitigation path.

There is no `nextion_hmi_shutdown`, no coordinator/RX task exit acknowledgement, no queue destruction, and no event unsubscription path. Current startup is boot-only, so this is a source-mapped lifecycle limitation rather than a confirmed reachable teardown defect. Any future restart/reconfiguration/test harness must define a stop order before releasing HMI resources.

## Findings and verification gaps

- Existing F-014, F-022–F-024, F-026–F-027, F-032–F-034, and F-050 cover the source hardening and remaining HMI/persistence risks.
- Panel-originated control lines use one bounded queue policy. Record queue-saturation behavior before deciding whether STOP/PAUSE/RESET need a distinct reliable ingress channel or priority policy.
- Verify disabled-HMI build behavior and use malformed-frame, NAK, partial-read, delete/rename, and power-loss fault-injection before making panel storage claims.
- Hardware/panel validation remains required for UART framing, display response timing, SD semantics, and reset output timing.

## Next verification slice

Map resolved configuration, component source declarations, and build-profile drift; then return to device-table destruction and HMI lifecycle with explicit fault-injection tests. No hardware action is authorized.

## Wave-2E external-HMI reconciliation (2026-09-15)

The read-only E review mapped current panel operations to a later
controller-owned control/status boundary; see [BHV-HMI-001–007 and proposed
scenarios](CURRENT-CONTROLLER-BEHAVIOR.md). This does not choose a transport,
wire format, or authorize a rewrite. START/PAUSE/RESUME/STOP currently pass
through HMI-local active/paused flags and a bounded HMI queue before the
dispatcher. Dispatcher enqueue success means admission, not coordinator
application. The external protocol must derive command preconditions/results
from controller state and keep STOP independent of stale display flags.

Save validation uses HMI ambient preference and differs from relaxed run
validation; manual mode destructively replaces the shared HMI draft with a
seven-day one-stage program and uses a latest-value-wins live target/rate
mailbox. Panel `.prg` files, NVS `prog_registry`/`user_prefs`, and a legacy
integer `delta=` parser are compatibility inputs, not automatically
controller-owned storage requirements. Fan Max/Silent preference is persisted
but does not command the physical fan. Service hours advance from an HMI worker
tick, including pauses, and can undercount multi-second stalls or vanish in a
disabled-HMI build; its product definition remains open.

E candidates for D/F/product reconciliation: `E-CAND-STATE-001` is a reachable
F-014 residual when a dropped lifecycle event leaves local run flags stale and
can make the HMI reject STOP or choose the wrong pause action;
`E-CAND-SERVICE-001` undercounts elapsed service time during worker stalls;
`E-CAND-RESET-001` ignores panel-delete/NVS-preservation failure outcomes.
[F-073](BUG-073-nextion-factory-reset-stack.md) is independently confirmed:
the tracked HMI-enabled factory-reset path enters a 12,896-byte compiled
delete-all frame on an 8,192-byte worker stack. Direct heater inhibit is
requested before that call, but electrical state and reset outcome remain
unverified. The operating oven has no independent hardware protections and
ESP32 checks are not certified safety functions.

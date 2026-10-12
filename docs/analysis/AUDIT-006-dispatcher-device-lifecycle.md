# AUDIT-006 — Dispatcher, Event, and Device-Table Lifecycle

**Scope:** source-only FreeRTOS lifecycle map of the command dispatcher, private
event loop, device-manager worker, and temperature-processor teardown.

**Method and limit:** current source was inspected; no production code was
changed and no hardware was operated.  A worker exit semaphore proves that one
worker reached its exit point.  It is not a producer-quiescence protocol and it
does not make unsynchronised device-table access safe.

## Ownership and lifetime map

| Resource | Owner | Active users/producers | Stop/acknowledgement | Destruction / gap |
| --- | --- | --- | --- | --- |
| Dispatcher context, queue, handler table | `commands_dispatcher` | All callers of `commands_dispatcher_dispatch_command`; dispatcher task reads queue and invokes handler arguments | `dispatcher_running=false`, task gives `exit_semaphore` | Owner joins the task, then clears handlers, deletes queue/semaphore and frees the context.  External callers have neither admission exclusion nor a stop acknowledgement. |
| Private ESP event loop | `event_manager` | All `event_manager_post*` callers; subscribed callbacks execute in the loop task | No application-level producer stop or callback-drain contract | `event_manager_shutdown` directly deletes the loop.  The public post/unsubscribe wrappers dereference the global handle without an initialized/handle check (except subscribe). |
| Device table | `device_manager` | Device-manager task traverses and invokes `device->ops->update(device->ctx)`; creation, state change, read/write, and destroy APIs mutate/read table entries | Manager stop wakes and joins its worker | Table entries are changed without a mutex, ownership token, reader count, or manager-worker quiescence requirement. |
| Temperature processor context and sensor pool entries | `temperature_processor_component` / `temp_sensor_device` | Processor task reads sensor cache; event callback notifies its task; device-manager worker updates static sensor contexts | Processor unsubscribes, signals worker, waits for its exit semaphore | It then destroys sensors while the device manager may still invoke an update on those same contexts. |

## Verified lifecycle paths

### Dispatcher

The dispatcher now correctly waits for its own worker: shutdown clears the
atomic running flag and waits for the exit semaphore before deleting the queue
and context (`commands_dispatcher_task.c:108-126`,
`commands_manager_core.c:112-149`).  The worker checks that flag at the top of
its receive loop and signals before self-deletion
(`commands_dispatcher_task.c:56-79`).  Re-entrant work from the dispatcher
task executes inline, avoiding the prior sole-consumer queue-full cycle
(`commands_manager_core.c:92-105`).

That join covers only the consumer.  External callers first load the global
pointer/running flag, then later dereference the same global queue handle in an
unbounded `xQueueSend` (`commands_manager_core.c:77-105`).  No lock, reference
count, gate, or producer acknowledgement spans that interval.

### Event manager

The manager owns an ESP event-loop handle and deletes it directly on shutdown
(`event_manager.c:54-68`).  It offers immediate and `portMAX_DELAY` posting
wrappers (`event_manager.c:111-166`).  There is no repository-level shutdown
order that first stops all posting tasks/callback sources; `main/main.c` has no
shutdown path.  The public `event_manager_post` and `event_manager_unsubscribe`
wrappers use the global handle without checking initialized state or a null
handle (`event_manager.c:95-125`).

This establishes an unclosed lifecycle contract, but not a separately numbered
source defect: whether a concurrent ESP-IDF event API operation is protected
by the upstream implementation requires an ESP-IDF API/lifecycle guarantee and
runtime characterization.  Until such a contract exists, event-loop shutdown
must be ordered after all application producers and subscribers are stopped.

The temperature processor's subscription is additionally weakly error handled:
both init and shutdown discard `event_manager_subscribe`/`unsubscribe` results
and return `ESP_OK` (`temperature_processor_events.c:52-61`).  This corroborates
the previously recorded event-subscription verification gap; it is not counted
again here as a new confirmed finding.

### Temperature processor and device manager

Temperature-processor shutdown removes its event subscription, wakes and joins
its own worker, then releases every owned sensor device
(`temperature_processor_core.c:80-108`, `:135-150`).  In contrast,
`device_manager_shutdown` is never called from this path
(`device_manager_core.c:153-164`).  The device-manager worker may therefore
still have accepted an entry as `RUNNING`, validated its operations, and be in
`device->ops->update(device->ctx)` (`device_manager_task.c:36-62`).

`temp_sensor_destroy` clears the static sensor entry's `allocated` and `valid`
flags before clearing the corresponding table entry's state/ops
(`temp_sensor_device_core.c:67-79`; `device_manager_core.c:135-149`).  An
in-flight `temp_sensor_update` performs the validity check, makes a blocking
Modbus call, and subsequently writes the cached reading/tick/valid flag
(`temp_sensor_device_core.c:178-198`).  None of those accesses use a shared
synchronisation primitive.  The static pool allocator can reuse the entry as
soon as `allocated` becomes false (`temp_sensor_device_core.c:26-64`).

## Confirmed defects proposed for the findings register

Historical crosswalk: the provisional `F-061` and `F-062` labels in this audit
were allocated before the central register's later numbered STOP finding.
The dispatcher-producer trigger below is current [F-062](FIRMWARE_FINDINGS.md),
and the device-destruction trigger is current [F-063](FIRMWARE_FINDINGS.md).
Current F-061 instead concerns a STOP-woken final control tick. These
paragraphs preserve original investigation evidence, not duplicate IDs.

### Historical proposed F-061 → current F-062 — Dispatcher producers

- **Category/confidence:** confirmed defect / high lifecycle; safety impact is
  conditional on the particular queued command and is not a hardware claim.
- **Evidence:** `commands_dispatcher_dispatch_command` checks
  `commands_dispatcher_ctx` and `dispatcher_running`, then later dereferences
  `commands_dispatcher_ctx->command_queue` for an unbounded send
  (`commands_manager_core.c:77-105`).  Shutdown sets running false, waits only
  for the consumer, then deletes that queue/semaphore and frees the same global
  context (`commands_manager_core.c:112-149`).
- **Reachable interleaving:** (1) external producer P passes lines 79-83;
  (2) shutdown S clears running and joins dispatcher D; (3) S deletes the
  queue and frees/NULLs the context; (4) P resumes at line 97 or 102 and reads
  freed global context memory (or blocks in the queue send while shutdown has
  no producer acknowledgement).  Concurrent init in the interval after D
  exits and before S frees the context has the same missing serialization.
- **Impact:** use-after-free/undefined behavior or a shutdown that cannot
  establish all command producers have stopped.  A command that was accepted
  immediately before shutdown can still be queued; the source does not define
  its cancellation/ordering semantics.  Existing direct heater inhibits remain
  the physical-output mitigation; this finding does not claim that an output is
  energized by the race.
- **Smallest direction:** introduce a dispatcher lifecycle state owned under
  one lock (or equivalent admission/reference protocol): reject new producers
  once stopping begins, wait for all admitted producers to exit the send path,
  then join/delete.  Give shutdown and init mutually exclusive ownership;
  bound/cancel producer sends according to an explicit command policy.
- **Deterministic oracle:** a host FreeRTOS seam pauses P after its admission
  check.  Invoke shutdown, then release P.  A correction must complete shutdown
  without P dereferencing/deleting a live queue, return a documented rejection
  or completion result, and permit a clean re-init.  Run under ASan/heap-poison
  where available and assert no handler executes after teardown begins unless
  the contract explicitly permits it.

### Historical proposed F-062 → current F-063 — Device release/update

- **Category/confidence:** confirmed defect / high sensor-lifecycle and
  control-input integrity.  It is source evidence, not proof of physical
  furnace behavior.
- **Evidence:** processor shutdown joins only its own task and calls
  `destroy_devices` while device manager remains active
  (`temperature_processor_core.c:80-108`).  The manager invokes update through
  a raw table pointer (`device_manager_task.c:36-62`).  Destroy clears both the
  sensor-pool flags and device-table state without synchronization
  (`temp_sensor_device_core.c:67-79`; `device_manager_core.c:135-149`), while
  update later writes that sensor context after Modbus returns
  (`temp_sensor_device_core.c:178-198`).
- **Reachable interleaving:** (1) manager M reads a `RUNNING` sensor entry and
  enters `temp_sensor_update`; (2) M passes its validity check and blocks in
  `ms9024_read_float`; (3) processor shutdown T joins its own worker and sets
  that sensor `allocated=false`, `valid=false`, and table state uninitialized;
  (4) a re-init may allocate the now-free static pool entry; (5) M resumes and
  writes its old transaction's temperature/tick/valid fields into the destroyed
  or newly allocated logical sensor.
- **Impact:** data race/undefined behavior and stale transaction data can be
  attributed to a replacement sensor.  It can corrupt aggregate freshness and
  validity inputs.  The coordinator's independent aggregate-expiry inhibit
  remains relevant but does not make this lifecycle race safe.
- **Smallest direction:** make device-manager ownership explicit.  Either stop
  and join the manager before destroying/reusing devices (with a documented
  restart order), or provide a manager-owned remove operation that prevents new
  update calls and waits for the active update to finish before releasing the
  sensor-pool entry.  Protect table fields and context lifetime as one protocol;
  a volatile flag or delay is insufficient.
- **Deterministic oracle:** mock `ms9024_read_float` to block after the updater
  has accepted a device.  In parallel, invoke temperature shutdown and attempt
  re-init.  The corrected contract must either defer destruction/reuse until
  the update exits or prevent the update from starting; assert no update writes
  to a removed/reused generation, the manager count/table are coherent, and
  every replacement sample has its own generation/freshness identity.

## Additional verification needed

- Characterize ESP-IDF's exact guarantees for unregister versus an in-flight
  callback and for event-loop deletion versus blocked/concurrent post calls.
- Test dispatcher stop/restart with an external queue producer blocked on a
  full queue, separately from the already-fixed dispatcher-task self-submit
  case.
- Test device creation/state-change/destroy concurrently with a manager tick,
  including errors from Modbus and partial temperature-processor initialization.
- Any correction that changes the device update/remove ordering must be
  reviewed against temperature validity/freshness and the heater's direct
  sensor-data inhibit, then receive powered-controller validation.

## Wave-2D concurrency reconciliation (2026-09-15)

The unchanged-baseline D review confirmed current F-061/F-062/F-063 and
source-verified [F-072 destroy-before-join](BUG-072-profile-stop-destroy-before-join.md),
[F-074 high-write ordering](BUG-074-heater-high-write-after-inhibit.md), and
[F-075 blocking heater telemetry](BUG-075-heater-telemetry-blocks-off.md).
The [current behavior draft](CURRENT-CONTROLLER-BEHAVIOR.md) carries the
output/state/temperature trace. No firmware or hardware action occurred.

| Owner / state | Contexts and current primitive | Unclosed lifetime or admission contract |
| --- | --- | --- |
| Dispatcher queue/handler table | External producers, sole consumer, registration/shutdown; atomic running flag and consumer exit semaphore | No admitted-producer or in-flight-handler drain before queue/context/handler-argument teardown (F-062; `D-CAND-HANDLER-LIFE`). |
| Coordinator profile/PID/task state | Dispatcher, control worker, temperature event callback, ESP_TIMER_TASK; temp snapshot mutex, target mailbox mutex, sensor atomics | `running`, `paused`, task state and file-static PID/profile are ordinary cross-context state; STOP can free/reset them before worker join (F-061/F-072). |
| Coordinator expiry timer/task handle | ESP_TIMER_TASK callback, event-loop arming, STOP/completion worker | Ordinary STOP deletes PID timer but leaves sensor-expiry timer; callback handle check does not pin worker lifetime (`D-CAND-TASK-HANDLE`). |
| Heater output state | PWM, heater command handler, sensor/STOP callers; power mutex and per-write GPIO mutex | Check+high GPIO write not atomic against direct inhibit (F-074); output I/O waits for telemetry queue (F-075); normal heater shutdown leaves handler argument alive until context free (`D-CAND-HANDLER-LIFE`). |
| HMI UART/queue/transfer flags | RX, HMI worker and event-loop bridges; recursive UART mutex, 8-entry queue | `volatile` transfer flags are hints, not synchronization; workers have no stop/join path, and dropped lifecycle event can leave display flags stale. |

`D-CAND-SENSOR-SPLIT` is a distinct fail-closed state-split interleaving: an
expiry callback sets coordinator sensor flags before setting the heater gate;
a recovery-ready validity event can clear the gate/flags during that gap;
then the older callback reasserts only the heater gate. Coordinator flag stays
false, so later validity events skip its recovery branch. A barrier test must
assert one coherent generation/reason state after either ordering; F-069 is
the separate automatic-resume policy defect.

`D-CAND-TASK-HANDLE` requires timer callback and worker completion overlap:
callback passes a non-null task-handle check, worker self-stops/clears/deletes,
callback notifies the retired handle. Full `stop_coordinator` stops the sensor
timer earlier; ordinary STOP does not. Confirm ESP timer callback-drain
guarantees and mock the check/delete/notify barrier before a permanent ID.

`D-CAND-HANDLER-LIFE` covers two public teardown edges not exercised by
normal `app_main`: `stop_coordinator` unregisters but does not join an entered
dispatcher handler before freeing its argument; heater normal shutdown frees
the raw handler argument without unregistering. A post-teardown HEATER_START
has conditional GPIO-high relevance. `D-CAND-QUERY-SNAPSHOT` is a future
external-HMI GET_STATUS prerequisite: public query reads ordinary task-state
fields concurrent with control writes, but no in-tree GET_STATUS producer was
found. Mock handler barriers and coherent snapshots before exposing it.

Queues and stacks in [baseline](ANALYSIS-BASELINE.md) are configured budgets,
not occupancy, free heap or high-water measurements. F-073's oversized HMI
factory-reset frame is one independently confirmed static/compiled exception.
No `volatile` flag, worker notification, or delay supplies a lifetime protocol.
The currently operating oven has no independent hardware safety protection;
all off/on conclusions here remain GPIO software requests.

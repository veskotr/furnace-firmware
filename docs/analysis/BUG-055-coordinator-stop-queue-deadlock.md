# BUG-055: Coordinator stop can deadlock on a full dispatcher queue

- **Subsystem:** Coordinator control task and command dispatcher
- **Severity:** high
- **Confidence:** confirmed
- **Status:** source fix implemented; regression validation pending
- **Affected files and symbols:** `components/coordinator_component/src/coordinator_component_heater_controller.c:stop_heating_profile/heater_controller_task`; `components/commands_dispatcher/src/commands_manager_core.c:commands_dispatcher_dispatch_command`
- **Prerequisite or linked IDs:** F-002, F-003, F-007, F-008, F-055
- **Ready to fix:** implemented; regression pending

## Observed behavior

The dispatcher avoids self-enqueue deadlock by executing submissions inline only when the dispatcher task is the caller. During an external STOP handled by that dispatcher, `stop_heating_profile` waits for the coordinator worker to exit. The worker then enters the same stop routine and submits final heater commands as an external producer, where a full queue blocks it indefinitely.

## Expected behavior

Coordinator stop/restart must complete without requiring a worker to submit work to the sole consumer that is waiting for that worker's exit acknowledgement.

## Reproduction

1. Fill the bounded dispatcher queue with commands.
2. Have the dispatcher begin processing an external coordinator STOP command.
3. The handler asserts inhibit and waits for the coordinator worker acknowledgement.
4. The worker exits and calls `stop_heating_profile`, then blocks in `xQueueSend(..., portMAX_DELAY)` while sending final heater commands.

The dispatcher cannot drain the queue because it is waiting for the worker; the worker cannot acknowledge exit because it is waiting for the dispatcher.

## Evidence

- `stop_heating_profile` sends `HEATER_CLEAR` and `HEATER_STOP` before waiting/acknowledging.
- `commands_dispatcher_dispatch_command` uses `xQueueSend(..., portMAX_DELAY)` for every non-dispatcher caller.
- The coordinator worker is not the dispatcher task and calls `stop_heating_profile` on its self-exit path.

## Root-cause hypothesis

The acknowledgement protocol is layered around a queue whose sole consumer synchronously waits for the producer being joined. Final command production has no shutdown-safe path.

## Violated invariant

No worker may require a bounded queue's sole consumer to make progress while that consumer is waiting for the worker to exit.

## Minimal root-fix boundary

Coordinator stop/worker-exit protocol and dispatcher submission contract. The physical heater inhibit API remains the immediate safety boundary.

## Concurrency or timing relevance

This is a deterministic two-task queue-full interleaving. Direct inhibit currently executes before the wait, so the deadlock is lifecycle/availability rather than a demonstrated re-energization path.

## Safety impact

High: direct inhibition reduces immediate heating risk, but a wedged stop/restart path leaves the controller in an indeterminate operational state and invalidates teardown guarantees.

## Implemented minimal fix

`stop_heating_profile` performs no dispatcher submission for either an external stop owner or the coordinator worker's own task. The direct control inhibit is asserted first, clearing the target and de-energizing SSR/contactor independently; the next profile start reinitializes heater command state. This keeps the entire stop/join protocol independent of queue availability.

## Applied scope and correction authority

The narrow queue-independent stop correction was implemented in the current hardening session. Queue-full stop/restart regression coverage remains required before release.

## ADR required, link, and status

No ADR is needed for a local stop-protocol correction that preserves ownership and message contracts. An ADR is needed if task/dispatcher ownership or command contracts change materially.

## Regression test

### Failure-before oracle and result

Mock a full command queue, invoke STOP through the dispatcher, and require bounded completion of the coordinator join. Source inspection confirms the pre-fix blocked worker/dispatcher cycle and the fixed path has no queue submission from the worker self-exit; an executable regression is not available in the current repository.

## Hardware validation

### Permission and required test level

No hardware operation is authorized by this report. Software coverage proves queue progress only; powered-controller validation still owns physical-off timing.

## Dependencies

F-002, F-003, F-007, F-008, F-041, and test infrastructure WI-001.

## Remaining uncertainty

Other producers can also block in unbounded dispatcher sends during global shutdown; this report confirms only the coordinator self-exit interleaving.

## Resume conditions

Resume with explicit correction authority, a queue-full regression seam, and documented ownership of final heater actions.

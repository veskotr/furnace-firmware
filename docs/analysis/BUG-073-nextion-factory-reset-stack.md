# BUG-073: Factory-reset delete-all frame exceeds HMI task stack

- **Subsystem:** enabled Nextion HMI, panel program storage, configured memory
- **Severity:** high conditional active-feature crash
- **Confidence:** confirmed source/configuration; historical object corroborates frame size
- **Status:** confirmed; correction not authorized
- **Affected files and symbols:** `hmi_coordinator_task`, `nextion_event_handle_line`, `handle_factory_reset_confirm`, `nextion_storage_delete_all_programs`
- **Prerequisite or linked IDs:** F-018 reset inhibit; F-026 registry enumeration; F-068 logger partition is unrelated
- **Ready to fix:** no; bounded delete strategy and test seam need impact review

## Observed behavior and reachable trigger

With the tracked HMI-enabled configuration, a `factory_reset_confirm` panel
line routes through the HMI coordinator task to the delete-all function. That
function allocates `char names[CONFIG_NEXTION_MAX_PROGRAMS][64]` as an automatic
local array. Selected maximum is 200, giving 12,800 bytes **before** caller
frames, while the HMI coordinator task stack is configured at 8,192 bytes.
The path is reachable even when the registry is empty because the automatic
frame is entered before any per-program loop. A crash/stack-canary trip has
not been observed in the field.

## Evidence and memory ownership

[HMI coordinator](../../components/nextion_hmi/src/coordinator/hmi_coordinator.c)
creates its worker using `CONFIG_NEXTION_COORDINATOR_TASK_STACK_SIZE` and
routes received lines in that task.
[Line router](../../components/nextion_hmi/src/events/nextion_events.c)
matches the exact destructive command and calls
[factory reset](../../components/nextion_hmi/src/events/nextion_settings_handlers.c),
which requests direct heater control inhibit before calling
[delete-all](../../components/nextion_hmi/src/storage/nextion_storage.c).
Resolved [sdkconfig.defaults](../../sdkconfig.defaults) sets maximum 200,
stack 8,192 bytes and stack canaries. Local ESP-IDF 5.5.4 `freertos/task.h`
states `xTaskCreate` stack depth is **bytes**. The stored baseline compiled
object (not a new build) shows a 12,896-byte delete-all frame. A canary may
detect corruption; it cannot make an oversized frame safe. Deployed image/
configuration remains unknown despite the user's field-settings description.

## Root cause and violated invariant

The fixed-size copy of the whole registry is allocated on a task stack whose
configured capacity is smaller than that single function frame. The memory
invariant is: every reachable frame and nested call fits its owning task's
stack with explicit margin, or uses bounded/owned storage with failure paths.

## Minimal root-fix boundary and safety impact

Characterize registry deletion semantics and use a bounded iteration/copy
strategy rather than a max-sized task-local array; preserve behavior when a
panel delete or NVS operation fails. A direct output-inhibit request precedes
the oversized frame, but source review cannot prove it completed electrically
or that a later reset is safe. The current oven has no independent hardware
safety protection. This is distinct from F-026's out-of-registry enumeration
and F-018's reset output response.

## Allowed scope, ADR, and correction authority

Analysis/documentation only. A narrow memory correction requires explicit
authorization, HMI/storage lifecycle impact review, a static/frame-size
regression and mocked reset/failure path. No ADR is needed for a purely local
bounded implementation; escalate if program-registry ownership or reset
semantics change.

## Regression test and hardware validation

`SCN-F-001`: static/compiled frame-versus-configured-stack check and a mocked
factory-reset path with empty/full registries and injected panel delete
failure. Failure-before: compiled frame alone exceeds task budget; passing
correction: bounded frame plus explicit partial-failure outcome and no
unbounded task-local copy. No test/build was run in this wave; historical
object evidence is not a new firmware result. Physical reset/output timing
requires a separate authorized powered-controller plan; no hardware action
is authorized.

## Resume conditions

Confirm current program registry/partial-delete requirements, choose bounded
storage ownership, and authorize a focused correction with test seam. Do not
infer deployed stack high-water from the stored object or configuration.

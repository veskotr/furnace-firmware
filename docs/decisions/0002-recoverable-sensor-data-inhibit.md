# ADR-0002: Recoverable sensor-data inhibit

- **Status:** accepted
- **Date:** 2026-07-13

## Context

F-001 requires control to stop when temperature data is invalid or stale, while F-017's GPIO/SSR failure inhibit must remain permanent. The present code has cached bare-float temperature events and no distinction between inhibit reasons.

## Problem

Sensor-data loss must remove output immediately and recover only after stable valid aggregate data, without clearing a hardware safety inhibit or depending on HMI/dispatcher activity.

## Constraints

- Sensor owns physical-read timestamps; processor owns aggregate validity; coordinator owns pause/recovery count; heater owns physical enforcement.
- HMI/dispatch cannot clear inhibits or resume safety-paused operation.
- No fault-manager or per-board configuration redesign in this patch.
- HMI contract remains unchanged; future HMI work must document its state/event contract first.

## Considered options

1. One shared inhibit boolean.
2. Queue a heater-clear command on timeout.
3. Separate permanent/recoverable inhibit reasons, with direct output enforcement and coordinator-owned recovery.

## Decision

Choose option 3. Sensor invalidity/staleness creates a recoverable sensor-data inhibit. GPIO/SSR/output-integrity failures remain permanent. The heater refuses demand while either reason applies; releasing sensor-data inhibition never releases permanent inhibition.

## Rationale

One boolean allows one fault to clear another; queued clearing inherits F-002/F-003. Separate reasons make physical ownership explicit while preserving the current architecture.

## Consequences

- Invalid aggregate: direct zero demand/output inhibit, coordinator pause, and paused control/stage/soak timing.
- Recovery requires configurable consecutive valid aggregate updates (default 3); invalidity resets the count.
- Coordinator alone requests recoverable-inhibit release, then resumes only if release succeeds and no other inhibit exists.
- Startup remains inhibited until valid aggregate recovery criteria pass.

## Migration plan

Add Kconfig stale timeout, recovery count, and auto-recovery enable; add sensor sample timestamps/processor aggregate validity; add recoverable heater inhibit; document HMI integration as deferred.

## Rollback strategy

Revert as one change set; do not replace it with queued output removal alone.

## Safety implications

Physical output removal must precede telemetry and be independent of HMI/dispatcher. Powered-controller validation is required.

## Test implications

Test stale/invalid aggregate, recovery-count reset, permanent-inhibit preservation, startup gate, and stage-time pause. Hardware test output-off latency and sensor-loss/recovery.

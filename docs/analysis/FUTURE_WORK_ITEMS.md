# Deferred engineering work items

These are planning items only. They authorize no firmware, configuration, hardware, dependency, or deployment changes. Sequence them after the current hardening work is characterized.

## WI-001: Establish a firmware test strategy and test infrastructure

- **Priority:** high
- **Purpose:** create a repeatable test pyramid for current and future firmware work.
- **Scope:** host unit tests for pure control/profile/parsing logic; ESP target-unit tests for component boundaries; mocked integration tests for FreeRTOS/events/commands/Modbus/GPIO; device-bench, powered-controller, and powered-furnace validation categories.
- **Acceptance outline:** documented test taxonomy and commands; CI executes deterministic host/target suites where available; each change records its test level; mocks are not presented as hardware proof.
- **Dependencies/decisions:** test runner/layout, dependency policy, fault-injection seams, CI environment, and retained test artifacts.
- **Related findings:** F-039, F-040, F-041, F-052.

## WI-002: Add a safe firmware test mode

- **Priority:** high
- **Purpose:** allow deterministic target and bench tests without accidentally operating production outputs.
- **Scope:** a separately identified build mode with controlled fakes/stubs for physical I/O, test-only command/telemetry hooks, and unambiguous boot/log identification.
- **Acceptance outline:** production builds cannot enable test behavior accidentally; test mode has explicit output policy, test identity, and documented flash/rollback procedure; it does not bypass electrical interlocks or substitute for powered validation.
- **Dependencies/decisions:** build-profile contract, GPIO/Modbus/time abstraction seams, test authorization, and hardware safety review.
- **Related findings:** F-017, F-018, F-041, F-052.

## WI-003: Define reproducible build profiles, including production

- **Priority:** high
- **Purpose:** make development, test, bench, and production images intentional, reviewable, and reproducible.
- **Scope:** tracked configuration policy/defaults, effective-symbol verification, build identity/versioning, artifact naming, compiler/ESP-IDF provenance, and release build commands.
- **Acceptance outline:** each image identifies its profile, commit, resolved configuration, and toolchain; production builds have an approved configuration source; CI detects drift between source defaults and effective configuration.
- **Dependencies/decisions:** configuration artifact format, secrets/signing policy, CI/release process, and migration behavior for existing boards.
- **Related findings:** F-040, F-049.

## WI-004: Define production logging and diagnostic-data management

- **Priority:** medium-high
- **Purpose:** preserve actionable fault evidence in production without compromising timing, storage endurance, privacy, or safety behavior.
- **Scope:** log levels by build profile, structured fault records, ring-buffer/crash-dump retention, export/collection procedure, storage quotas/rotation, rate limiting, and failure behavior when storage is unavailable.
- **Acceptance outline:** production logging is bounded and non-blocking for control; error/fault records retain firmware/config identity; operational staff have a documented collection and interpretation process.
- **Dependencies/decisions:** future fault-state design, storage wear budget, security/privacy requirements, maintenance interface, and test-mode telemetry policy.
- **Related findings:** F-014, F-035, F-036.

## WI-005: Define per-board configuration and identity

- **Priority:** high
- **Purpose:** support several controllers without silently mixing pin mappings, calibration, hardware capabilities, or deployment history.
- **Scope:** board identity, hardware revision, approved pin map, sensor/SSR/contactor polarity, calibration/configuration provenance, capability flags, validation status, and provisioning/update workflow.
- **Acceptance outline:** every board has a versioned configuration record; incompatible firmware/configuration combinations are rejected or visibly fail safe; production images record which board configuration they target; field changes are auditable and recoverable.
- **Dependencies/decisions:** configuration storage/format, provisioning authority, schema migration, signing/integrity model, factory-reset behavior, and hardware validation process.
- **Related findings:** F-016, F-042, F-049.

## Suggested order after hardening

1. WI-001 and WI-003 together, so test results always name the exact effective build.
2. WI-002, using the safety boundaries and build profiles from the first step.
3. WI-005, before multi-board deployment or field configuration changes.
4. WI-004, coordinated with the later fault-state/HMI architecture.

No item above replaces the existing requirement for independently authorized device-bench or powered-controller validation.

---
name: review-furnace-safety
description: Review safety paths for heater, SSR, relay, fan, airflow, temperature, profiles, watchdog, reset, and fault handling. Use whenever work can energize or fail to de-energize physical outputs. Do not use as a generic style review or as authorization to modify/operate hardware.
---

# Review Furnace Safety

## Inputs

Require review scope/change, relevant configuration, hardware assumptions/schematic if available, and intended operating/fault states.

## Workflow

1. Trace every software path that can energize contactor/SSR and every path intended to remove power.
2. Build a GPIO ownership table comparing Kconfig defaults, current resolved config, every software writer, active polarity, and the deployed schematic/pin plan; then check boot, reboot, partial initialization, shutdown, invalid/stale sensors, communication loss, overtemperature, airflow/fan interlocks, pause/cancel/completion, fault acknowledgement/recovery, and watchdog reset.
3. Verify safety gating at the actuator boundary and independence from congested queues/telemetry.
4. Enumerate single failures, stale in-flight work, and configuration/pin collisions; separate software proof from external-hardware assumptions.
5. Rank evidence by uncontrolled-heating potential and define appropriate regression/hardware validation.

## Required output

Produce safety invariants checked, energize/de-energize path inventory, evidence-backed findings with confidence, hardware assumptions, missing evidence, and required validation.

## Stop and safeguards

Stop and flag a release blocker when an output-off path is unproven. Do not infer fail-safe polarity/interlocks, equate logging with mitigation, edit production code during review, energize hardware, or call compilation a safety test. Update findings and maps only when authorized; verify against exact current symbols.

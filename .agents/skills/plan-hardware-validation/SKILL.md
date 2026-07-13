---
name: plan-hardware-validation
description: Plan safe device-bench, powered-controller, or powered-furnace validation for firmware behavior that software tests cannot prove. Use for GPIO polarity, relays/SSR, sensor loss, RS-485, airflow, thermal control, reset, watchdog, and real timing. Do not use as authorization to flash, energize, bypass interlocks, or operate a furnace.
---

# Plan Hardware Validation

## Inputs

Require change/finding, claimed behavior, schematic/hardware assumptions, available rig, safety authority, limits, observers/instruments, and abort controls.

## Workflow

1. Classify device bench, powered-controller, or powered-furnace level and justify why software evidence is insufficient.
2. Define prerequisites, safe initial state, instrumentation, independent cut-off, personnel/area controls, and configuration/firmware identity.
3. Specify ordered steps, expected measurements/tolerances, injected failures, data capture, stop criteria, and recovery.
4. Separate de-energized continuity/signal checks from powered/thermal checks; minimize energy and duration.
5. Provide result-recording fields and rollback/inspection after any failure.

## Required output

Produce level, prerequisites, hazards/controls, exact procedure, pass/fail criteria, abort/recovery, evidence to retain, and remaining limitations.

## Stop and safeguards

Stop if independent de-energization, qualified supervision, schematic/polarity, instruments, or explicit authority is missing. Never execute the plan implicitly, defeat interlocks, rely only on software stop, or claim planned work was performed. Attach results to the change validation report only after actual execution.

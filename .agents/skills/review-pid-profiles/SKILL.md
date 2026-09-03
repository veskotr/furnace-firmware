---
name: review-pid-profiles
description: Review PID computation, SSR windowing, profile stages, and their safety gating. Use for control changes, timing or unit questions, setpoint/stage behavior, pause/resume/cancel/completion, windup/clamping, or invalid input. Do not use for tuning by guesswork or hardware commissioning without an approved plan.
---

# Review PID and Profiles

## Inputs

Require configuration/gains, clock/sample/window assumptions, profile example, expected transitions, sensor-validity contract, and hardware limits if known.

## Workflow

1. Trace units and scale conversions from Kconfig/profile/storage to setpoint, `dt`, PID output, and SSR window.
2. Verify clock source, sample cadence/jitter, clamps, integral anti-windup, derivative handling, initialization/reset, and invalid/non-finite input.
3. Walk heating/holding/cooling/cooldown transitions, stage bounds, setpoint jumps, overshoot/stall, pause/resume/cancel, and completion.
4. Trace demand through safety authorization to contactor/SSR; identify stale in-flight commands and mismatched periods.
5. Define deterministic logic tests and separate them from powered thermal validation/tuning.

## Required output

Provide unit/timing table, transition table, safety-gate trace, evidence-backed findings/confidence, tests, hardware-validation needs, and uncertainty.

## Stop and safeguards

Stop before changing PID constants, hardware limits, or profiles without explicit authority and measurement evidence. Do not tune from static inspection, feed invalid input to prove safety, edit code during review, or call simulation physical validation. Update findings/maps when authorized.

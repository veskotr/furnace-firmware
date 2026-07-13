---
name: investigate-furnace-bug
description: Investigate a suspected furnace firmware defect without immediately editing production code. Use for failures, races, deadlocks, stale data, control anomalies, lifecycle faults, corruption, resets, and numbered findings needing confirmation. Do not use once a specific verified bug is ready for correction or for broad architecture review.
---

# Investigate Furnace Bug

## Inputs

Require symptoms or finding ID, expected behavior, available logs/configuration, reproduction context, and hardware state if relevant.

## Workflow

1. Reproduce safely or establish a reachable source-level trigger; never energize hardware without authorization.
2. Trace the full execution and physical-output path, including all readers/writers, owners, callbacks/tasks, timing, synchronization, and cleanup.
3. Separate observed symptom, violated invariant, proximate mechanism, and likely root cause.
4. Seek disconfirming guards/call sites and identify race interleavings, stale-state windows, and hardware assumptions.
5. Classify confidence and severity; propose the smallest safe correction direction, not a patch.
6. Create/update a report from `docs/templates/bug-report.md` and the findings index when documentation edits are authorized.

## Required output

Return exact files/symbols, reproduction/interleaving, evidence, violated invariant, root-cause hypothesis, confidence/category, safety/timing relevance, minimal root-fix boundary, prerequisite/linked IDs, ADR need/status, regression oracle and fail-before result, allowed scope/authority if known, hardware permission/test level, ready-to-fix yes/no, resume conditions, and uncertainty.

## Stop and safeguards

Stop if evidence cannot distinguish competing causes and name the next experiment. A finding is ready-to-fix only when the root boundary, allowed scope, correction authority, required ADR, and software regression oracle are settled. Powered hardware evidence may remain explicitly pending for code handoff, but it remains a release blocker when the safety claim requires it. Do not edit production code, label a bug confirmed from suspicion, or hide uncertainty. Verify line/symbol references and update an authorized existing finding/report status without deleting its history.

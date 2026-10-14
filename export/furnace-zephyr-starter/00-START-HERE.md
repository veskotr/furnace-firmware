# 00 — Start here: bootstrapping the new repository

Audience: the first agent or engineer working in the new, empty repository. Follow the steps in order. Every step that touches a deferred topic triggers the remind-and-ask rule ([01-RULES.md](01-RULES.md) §2).

## Step 1 — Place this folder

- Copy this whole folder into the new repository as `docs/starter/`.
- Treat `docs/starter/` as **frozen reference**: do not edit it to describe the new project. The new project's living documents are created in step 4. The starter keeps recording only what the old project was and what was decided at hand-over.

## Step 2 — Ask before choosing the agent tooling (deferred T-17)

The legacy project used OpenAI Codex conventions: `AGENTS.md`, `.agents/skills/*/SKILL.md` and `.codex/agents/*.toml`. Ask the product owner which tools the new repository will use (Codex, Claude Code, both, or another), because that decides file names and discovery paths. Do not create instruction files before this is answered.

## Step 3 — Write the new instruction file(s)

- Write the root instruction file (for example `AGENTS.md`, and/or `CLAUDE.md` pointing to the same rules) **from [01-RULES.md](01-RULES.md)**, adapted to the new repository's real paths.
- Do not copy the legacy instruction files in `reference/`. They describe an ESP-IDF/FreeRTOS hardening project; read them only to understand the intent behind the rules.
- The root instructions must contain, at minimum:
  - the safety non-negotiables;
  - the remind-and-ask rule;
  - the integration-pending rule;
  - the test-first rule;
  - the evidence taxonomy;
  - "do not flash or energize hardware without explicit permission".

## Step 4 — Create the living registers

- **Decisions and deferred topics:** create the new project's living register (for example `docs/decisions/DECISIONS-AND-DEFERRED-TOPICS.md`) by carrying over every D-entry and T-entry from [02](02-DECISIONS-AND-DEFERRED-TOPICS.md), unchanged in meaning. From then on, answers are recorded in the living register.
- **Requirements:** create the living requirements document from [06-REQUIREMENTS-BASELINE.md](06-REQUIREMENTS-BASELINE.md). Keep the REQ-IDs stable so tests can trace to them.
- **Findings / defects:** start an empty register with the new project's own ID space. Link back to "legacy F-xxx" labels only as provenance.
- **Integration-pending list:** generated from the code markers (see 01 §3), not hand-written.

## Step 5 — Author the new skills and agents

Follow [08-SETUP-FOR-NEW-REPO.md](08-SETUP-FOR-NEW-REPO.md) §4. Use `reference/legacy-agent-setup/` as **source material**: keep the patterns (inputs → workflow → output → stop conditions, reviewer/writer split, evidence levels) and rewrite the content for Zephyr and test-first work. Do not copy-paste them and do not modify the reference copies.

## Step 6 — Set up the toolchain, CI and verification

[08](08-SETUP-FOR-NEW-REPO.md) §2–§3: Zephyr workspace (west), host/`native_sim` test runner, CI build matrix, warnings as errors, static analysis, and a repository verification script that enforces the rules.

## Step 7 — Write the transition plan

Suggested phases (the plan itself belongs to the new repository):

| Phase | Content | Blocked by |
| --- | --- | --- |
| P0 | Repository setup (steps 1–6) | T-17 |
| P1 | Pure domain core, test-first on host / `native_sim`: program model and validation, ramp planner, stage state machine, PID + feedforward, guards, sensor aggregation and outlier exclusion, run-state machine | Nothing; deferred parameters stay configurable and marked |
| P2 | Platform abstraction interfaces (actuator, sensor bus, storage, clock, display link, logging) with fakes; integration markers where hardware is unknown | Nothing (interfaces only) |
| P3 | Board bring-up when MCU and schematic are supplied | T-12, T-15 |
| P4 | Sensor driver | T-07 |
| P5 | Controller ↔ display protocol | T-01, T-02 |
| P6 | Persistence and migration | T-03, T-13 |
| P7 | Closed-loop simulation against a plant model, then bench / powered-controller / powered-furnace validation with written plans | Hardware, authorization |

## Step 8 — Work rule for every later session

1. Read the root instructions and the living registers.
2. Check the deferred topics for the area you are entering.
3. Write the failing test first.
4. Mark integration gaps instead of guessing.
5. Record what you learned.

# Furnace controller (Zephyr) starter kit

**Read this first. This folder is guidance, not a template.**

It exists to guide agents and software engineers starting the **new** furnace-controller firmware (Zephyr RTOS, test-driven). It tells them:

- what the old firmware did (its final, field-used behavior);
- which lessons and mistakes to learn from;
- which setup the new repository needs;
- which rules to follow;
- which decisions are already made, and which are deliberately postponed.

It does **not** contain code to port, configuration to reuse, or agent instructions to install. The new project is designed from scratch. Nothing here is to be copied into the new codebase as-is.

| This folder is | This folder is not |
| --- | --- |
| A self-contained knowledge base, readable without the old repository | A copy of the old project |
| The input for writing the new project's rules, skills, agents and plan | A ready-made `AGENTS.md` / `CLAUDE.md`, skill set or agent set |
| A record of the old system's **final** behavior (as built and field-used) | A history of every intermediate version |
| A list of decisions, deferred topics and rules the new project must respect | Permission to decide deferred topics without the product owner |

## Provenance

- Derived from the legacy repository `furnace-firmware` (ESP32, ESP-IDF 5.5, FreeRTOS, Nextion HMI), branch `main`, commit `d1b0fda`, on 2026-10-13.
- Field facts come from the team's field-tuning notes (May–June 2026).
- Statements carry evidence labels:
  - **legacy-source**: read in the old code;
  - **field-reported**: from field runs, not re-measured;
  - **decided**: product-owner decision;
  - **deferred**: open topic;
  - **inferred**.
- No statement was proven by running firmware; the old project had no automated tests.
- Legacy finding numbers (for example "legacy F-057") are kept only as provenance labels. Each is fully described in [07-LESSONS-LEARNED.md](07-LESSONS-LEARNED.md); nothing requires the old repository to understand them.

## Reading order

| # | Document | Purpose |
| --- | --- | --- |
| 0 | [00-START-HERE.md](00-START-HERE.md) | Step-by-step bootstrap of the new repository (for the first agent/engineer session) |
| 1 | [01-RULES.md](01-RULES.md) | **Normative rules** for the new project: safety, architecture, integration-pending, remind-and-ask, evidence, process |
| 2 | [02-DECISIONS-AND-DEFERRED-TOPICS.md](02-DECISIONS-AND-DEFERRED-TOPICS.md) | What is decided (D-xx) and what must be asked when work reaches it (T-xx) |
| 3 | [03-LEGACY-SYSTEM-BEHAVIOUR.md](03-LEGACY-SYSTEM-BEHAVIOUR.md) | The old machine and firmware: I/O, method of operation, timing, persistence, HMI responsibilities |
| 4 | [04-CONTROL-ALGORITHMS.md](04-CONTROL-ALGORITHMS.md) | Exact profile, ramp, PID, feedforward, guard and actuation math; field-tuning note |
| 5 | [05-SENSING.md](05-SENSING.md) | Temperature acquisition, validity, freshness; MS9024 history; lessons for the new sensors |
| 6 | [06-REQUIREMENTS-BASELINE.md](06-REQUIREMENTS-BASELINE.md) | Target requirements (REQ-IDs) for the new controller, with decision/deferred references |
| 7 | [07-LESSONS-LEARNED.md](07-LESSONS-LEARNED.md) | Every legacy defect and mistake worth learning from, each with its lesson and first test |
| 8 | [08-SETUP-FOR-NEW-REPO.md](08-SETUP-FOR-NEW-REPO.md) | Repository setup, tooling, CI, verification, and how to author the new rules, skills and agents |
| 9 | [09-ZEPHYR-MAPPING-AND-TEST-BACKLOG.md](09-ZEPHYR-MAPPING-AND-TEST-BACKLOG.md) | Suggested module structure, Zephyr facilities, test layers, first test backlog |
| 10 | [10-CONFIGURATION-AND-PRECISION.md](10-CONFIGURATION-AND-PRECISION.md) | Configuration lessons and the precision inventory |
| R | [reference/legacy-agent-setup/README.md](reference/legacy-agent-setup/README.md) | **Read-only** copies of the old project's agent instructions, agents, skills, templates and verification script, as source material for writing new ones |

## Two rules that apply from the first minute

1. **Remind and ask.** If work touches a trigger area of a deferred topic in [02](02-DECISIONS-AND-DEFERRED-TOPICS.md), stop, remind the product owner of the topic and what is already decided, and ask. Never decide it silently.
2. **Integration pending.** Where the new board, MCU, schematic, HMI protocol, sensors, log format, cure-program format or configuration values are not yet known, do not invent them. Build against an interface, mark the gap with the integration marker defined in [01-RULES.md](01-RULES.md), and stop there.

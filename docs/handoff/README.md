# Rewrite handoff package

Status date: 2026-10-13. Source baseline: `main` at `d1b0fda` (tree identical to `196d2ff`). Field-tuned configuration: the local, untracked `sdkconfig` dated 2026-06-05 (see [05-CONFIGURATION-CATALOG.md](05-CONFIGURATION-CATALOG.md)).

## Purpose

This package records what the current ESP-IDF/FreeRTOS furnace firmware does, how it does it, how the team worked on it, and which mistakes are still in the code or the documentation. It is the starting point for a new project with the same responsibilities, with these planned differences:

- Rewritten for **Zephyr RTOS**.
- **Test-driven** from the first commit.
- **No Nextion HMI hosted on the controller.** Every responsibility that the Nextion component currently carries must move somewhere else; [07-HMI-CONTRACT-NEXTION.md](07-HMI-CONTRACT-NEXTION.md) lists them.
- **Changed precision.** The exact changes are not yet specified. [09-REWRITE-GUIDANCE-ZEPHYR-TDD.md](09-REWRITE-GUIDANCE-ZEPHYR-TDD.md) lists every precision-bearing quantity in the current design so those decisions can be made explicitly.

Nothing in this package is hardware validation. Every statement is labelled with its evidence level.

## Portable starter kit

This package is tied to this repository: it links to legacy docs and cites legacy source lines. The **self-contained version for the new repository** is `export/furnace-zephyr-starter/` (start with its `README.md`). It reproduces the needed content as text, describes only the final field-used behavior, records field tuning as a note, and holds the legacy agents and skills as read-only reference.

## Reading order

| # | Document | Use it for |
| --- | --- | --- |
| 1 | [01-SYSTEM-OVERVIEW.md](01-SYSTEM-OVERVIEW.md) | What the machine is, the I/O, and the end-to-end method of operation |
| 2 | [02-FUNCTIONAL-REQUIREMENTS.md](02-FUNCTIONAL-REQUIREMENTS.md) | Numbered, testable behavior contract (current behavior, intended behavior, known divergence) |
| 3 | [03-CONTROL-ALGORITHMS.md](03-CONTROL-ALGORITHMS.md) | Exact profile, PID, feedforward, guard and SSR-window math, plus the field-tuning history |
| 4 | [04-MODBUS-MS9024.md](04-MODBUS-MS9024.md) | RS-485 transport, MS9024 register work, float decoding, diagnostics and repair history |
| 5 | [05-CONFIGURATION-CATALOG.md](05-CONFIGURATION-CATALOG.md) | Every project configuration symbol, compared across Kconfig default, tracked defaults and the field build |
| 6 | [06-KNOWN-DEFECTS-AND-MISTAKES.md](06-KNOWN-DEFECTS-AND-MISTAKES.md) | Consolidated register of open defects, residual risks and mistakes left in place, each with a lesson and a test to write |
| 7 | [07-HMI-CONTRACT-NEXTION.md](07-HMI-CONTRACT-NEXTION.md) | Operator functions, persistence and business logic currently inside the Nextion component |
| 8 | [08-PROCESS-AGENTS-SKILLS.md](08-PROCESS-AGENTS-SKILLS.md) | Project timeline, the Codex agent/skill workflow, evidence taxonomy, and what to carry forward |
| 9 | [09-REWRITE-GUIDANCE-ZEPHYR-TDD.md](09-REWRITE-GUIDANCE-ZEPHYR-TDD.md) | Module-to-Zephyr mapping, architecture rules learned, first test backlog, precision surfaces, open decisions |
| 10 | [10-DECISIONS-AND-DEFERRED-TOPICS.md](10-DECISIONS-AND-DEFERRED-TOPICS.md) | **Decisions made (D-xx) and topics deliberately postponed (T-xx).** Every agent must check it and ask the user before working in a deferred area |

> **Remind-and-ask rule.** When work touches a trigger area of a deferred T-entry in document 10, stop, remind the user of the topic, and ask its questions before choosing a solution. The rule is also in [../../AGENTS.md](../../AGENTS.md), the entry skills and every agent definition, and `tools/codex/verify_repository_setup.py` enforces it. The successor project must copy it.

## Relationship to the existing documentation

| Existing document | Role now |
| --- | --- |
| [../codex/REPOSITORY_MAP.md](../codex/REPOSITORY_MAP.md) and [../codex/repository-map.yaml](../codex/repository-map.yaml) | Navigation map of the current source; re-checked on 2026-10-13 |
| [../analysis/FIRMWARE_FINDINGS.md](../analysis/FIRMWARE_FINDINGS.md) | Authoritative findings register; F-057 onward were added by this audit |
| [../analysis/CURRENT-FIX-STATUS.md](../analysis/CURRENT-FIX-STATUS.md) | Per-finding source status |
| [../decisions/](../decisions/) | ADR-0001 and ADR-0002 (carry their decisions forward; see the ADR-0002 implementation note) |
| `docs/analysis/BUG-*`, `CHANGE-VALIDATION-*`, `*-HARDWARE-VALIDATION.md` | Per-defect evidence, validation reports and unexecuted hardware plans |
| [../../CODE_ANALYSIS_REPORT.md](../../CODE_ANALYSIS_REPORT.md), [../../MODBUS_STUDY_PLAN.md](../../MODBUS_STUDY_PLAN.md) | Historical (Dec 2025 / Mar 2026). Leads only; architecture described there no longer exists |

## Evidence labels used in this package

| Label | Meaning |
| --- | --- |
| **source** | Read in current source at `d1b0fda`; file and symbol cited |
| **config** | Read from `sdkconfig.defaults` (tracked) or the local `sdkconfig` (field build) |
| **field-reported** | Reported from field runs in team notes; not independently re-measured |
| **inferred** | Reasoned from source, not executed |
| **unverified** | Needs hardware, a datasheet, or a decision |

No host or target test suite exists in this repository. No statement here was proven by executing firmware.

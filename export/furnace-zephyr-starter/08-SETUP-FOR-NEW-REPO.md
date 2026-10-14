# 08 — Setup for the new repository, and how to author its rules, skills and agents

## 1. How the legacy project worked (context)

| Period | What happened |
| --- | --- |
| Nov 2025 | Logger; first temperature path on SPI (MAX31865); coordinator, setpoint calculation, GPIO driver, basic PID |
| Dec 2025 – Jan 2026 | Large component refactor; health heartbeats; event-driven heater; unified error types (descriptor mechanism never populated) |
| Mar 2026 | Nextion HMI with program editor and storage; manual mode; **switch to Modbus MS9024 transmitters** with a device-manager model; command dispatcher |
| May 2026 | Stall and overshoot guards; crash-log storage |
| May – Jul 2026 | Field commissioning on 15 kW / 25 kW units: multi-sensor support, MS9024 register investigation, fan controller, PID/feedforward tuning rounds, soft landing, hold-deviation guard. Then an AI-agent-assisted **hardening pass** (31 fix and 11 documentation changes), decision records, bug and validation reports |
| Oct 2026 | Everything squash-merged into two commits; hand-over audit and this starter |

What worked:

- narrow, ID-tracked findings with trigger, impact and residual risk;
- decision records for safety boundaries;
- read-only reviewer roles separated from writer roles;
- explicit stop conditions in every workflow;
- per-tick control logging during tuning.

What did not work:

- **no executable tests at all**, so every fix ended "validation pending";
- documentation drifting from the code within weeks;
- rules written but not enforced by tooling;
- a tracked configuration that was not the field build;
- squash merges that lost history;
- features claimed but not wired.

## 2. Toolchain and workspace (to set up)

- **Zephyr:** pin one release and its SDK in a `west` manifest. Record the versions in the README and print them in the boot banner.
- **Targets:**
  - `native_sim` (host) for unit and integration tests from day one;
  - the real board once the MCU and schematic are supplied (T-12, D-14). Until then, no board-specific code; integration markers only.
- **Test tooling:** Zephyr `ztest` + `twister` on `native_sim`. FFF fakes, the GPIO emulator and UART emulation for driver boundaries. A plant-model simulation harness for closed-loop tests.
- **Quality gates in CI:**
  - warnings as errors;
  - format check (for example clang-format);
  - static analysis;
  - address/undefined sanitizers on host tests;
  - coverage report on the domain core;
  - build of every configuration variant.
- **Configuration:** one tracked configuration per board/furnace variant (R13). Production-build check: reject `*_PLACEHOLDER` symbols and enabled-path integration markers ([01](01-RULES.md) §3).

## 3. Repository verification script (to write, not to copy)

The legacy project had a verification script (reference copy in `reference/legacy-agent-setup/tools/`). It checked file presence, agent/skill sets, skill front-matter and links, but **did not enforce behavior rules**. The new one must at least check:

1. The root instruction file(s) contain the safety non-negotiables, the remind-and-ask rule, the integration-pending rule and the test-first rule.
2. The decisions register exists. Every T-ID referenced by an integration marker or placeholder exists and is `deferred`. Resolved T-IDs no longer have markers.
3. No plain `TODO` / `FIXME` in production code; only `TODO(INTEGRATION T-xx)`.
4. Every requirement ID in the requirements document is referenced by at least one test, or listed as "not yet covered" in a generated trace report.
5. Markdown links resolve; generated inventories (threads, channels, configuration symbols, integration markers) match the code.
6. Entry skills and agents reference the register and the rules.

## 4. Authoring the new instructions, skills and agents

**Do not copy-paste the legacy files and do not modify the reference copies.** Read them for intent and structure, then write new ones for Zephyr and test-first work. The choice of tool (Codex, Claude Code, both) is deferred topic T-17: ask first.

### 4.1 Patterns worth keeping (from the legacy set)

- Every skill: **Inputs → Workflow → Required output → Stop and safeguards**, with a precise "use when / do not use when" description.
- Reviewer roles are read-only; writer roles are separate.
- Evidence levels in every output; mocks are never hardware proof.
- "Ready-to-fix" gate before a fix: reachable trigger, root boundary, authority, regression oracle.
- Hardware validation is planned, never executed implicitly; independent cut-off required.
- Stop conditions on missing authority, missing evidence, or unresolved architecture.

### 4.2 Legacy inventory (for orientation)

| Legacy agent (read-only = R) | Role | Legacy skills used |
| --- | --- | --- |
| repository-analyst (R) | Discovery, path tracing, impact | orient-furnace-repository, analyze-firmware-impact |
| firmware-architect (R) | Ownership and boundaries; decision-record drafts | analyze-firmware-impact, decide-furnace-architecture |
| bug-investigator (R) | Evidence and root cause | investigate-furnace-bug |
| bug-fixer | Narrow verified fix per bug ID | fix-furnace-defect |
| feature-implementer | Bounded feature | implement-furnace-feature |
| test-engineer | Regression tests, validation plans | create-firmware-regression-test, plan-hardware-validation |
| refactoring-agent | Behavior-preserving refactor | refactor-furnace-behavior-preserving |
| safety-reviewer (R) | Every energize / de-energize path | review-furnace-safety |
| concurrency-reviewer (R) | Tasks, callbacks, queues, lifetimes | review-freertos-concurrency |
| control-system-reviewer (R) | PID, profile, SSR window | review-pid-profiles |
| modbus-device-reviewer (R) | Bus, device lifecycle, validity | review-modbus-devices |

Remaining legacy skills: review-furnace-release, maintain-furnace-docs-map.

### 4.3 What must change for the new project

| Legacy assumption | New project |
| --- | --- |
| Phase "harden, do not refactor" | Greenfield, **test-first**; every change starts with a failing test |
| FreeRTOS / ESP-IDF terms, `idf.py` | Zephyr terms (threads, `k_msgq`, zbus, `k_timer`/`k_work`, devicetree), `west`, `twister` |
| Modbus/MS9024 reviewer | Sensing reviewer for the new sensors (T-07): freshness, outliers (T-05), validity |
| Display was part of the firmware | Display is a remote client; a protocol reviewer/skill once T-02 is answered |
| Tests "where practical" | Tests mandatory; requirement traceability |
| Unknown hardware facts investigated in code | Unknown facts are **integration markers**; ask, never guess (D-14) |

### 4.4 Suggested new skill set (to write)

- **orient**: read the rules, the registers and the integration-marker list; route to a specialist.
- **remind-and-ask**: detect deferred topics touched by a task; present constraints and questions; record answers.
- **write-failing-test**: choose the layer, write the test for a REQ-ID, confirm it fails for the right reason.
- **implement-to-green**: the smallest change that passes; no unrelated edits.
- **refactor-on-green**: behavior-preserving clean-up with tests green before and after.
- **mark-integration-gap**: define the interface plus a fake, insert the marker and a fail-safe stub, register the gap.
- **requirements-trace**: map REQ-IDs to tests; report uncovered requirements.
- Reviews: **review-safety**, **review-concurrency**, **review-control**, **review-sensing**, **review-protocol** (after T-02).
- **decide-architecture** (with ADR), **investigate-bug**, **fix-defect-test-first**, **plan-hardware-validation**, **review-release**, **maintain-docs-and-registers**.

Agents: keep the reviewer/writer split. Each agent's instructions name its skills and repeat the remind-and-ask and integration-pending rules.

### 4.5 Templates (write new ones, using the legacy templates as reference)

Bug report, feature proposal (with REQ-IDs and tests), architecture decision record, change-validation report (executed / inspected / simulated / planned / unverified), hardware validation plan (prerequisites, independent cut-off, procedure, pass criteria, abort, evidence).

## 5. Living documents to create in the new repository

| Document | Seeded from |
| --- | --- |
| Root instructions (+ tool-specific pointer files) | [01-RULES.md](01-RULES.md) |
| Decisions and deferred topics (living) | [02](02-DECISIONS-AND-DEFERRED-TOPICS.md) |
| Requirements (living) | [06](06-REQUIREMENTS-BASELINE.md) |
| Architecture overview + ADRs | [09](09-ZEPHYR-MAPPING-AND-TEST-BACKLOG.md), [01](01-RULES.md) §5 |
| Findings / defects register (new ID space) | empty |
| Integration-gap list | generated from code markers |
| Test plan and trace report | [09](09-ZEPHYR-MAPPING-AND-TEST-BACKLOG.md) §5, generated trace |
| Transition plan | [00-START-HERE.md](00-START-HERE.md) step 7 |

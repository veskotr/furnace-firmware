# 02 — Decisions and deferred topics (starter copy)

This register has two parts:

- **D-entries:** product decisions already made.
- **T-entries:** topics deliberately postponed until development reaches them ("we will think of solutions when we come to this part").

This is the hand-over state of the register. In the new repository, carry every entry into the living register (see [00-START-HERE.md](00-START-HERE.md) step 4) and record new answers there, not here.

IDs such as "F-057" are legacy finding labels, each described in [07-LESSONS-LEARNED.md](07-LESSONS-LEARNED.md). Requirement IDs (SAFE-, TEMP-, GUARD-, MAN-, FAN-, PERS-, API-) refer to [06-REQUIREMENTS-BASELINE.md](06-REQUIREMENTS-BASELINE.md).

## Rule: remind and ask

Before planning, designing, reviewing or changing anything, check the **Trigger areas** of every T-entry with status `deferred`. If the task touches one:

1. Stop before choosing a solution in that area.
2. Remind the user of the topic, the constraints already decided (linked D-entries), and the evidence (F-IDs).
3. Ask the listed questions. Offer options and a recommendation if useful, but **do not decide for the user**. A missing answer is not permission.
4. Work outside the deferred area may continue. Clearly mark anything that depends on the answer as provisional.
5. When the user answers: add a D-entry with the date, set the T-entry to `resolved → D-xx`, and update the affected documents (requirements, guidance, findings).

The same rule is in [01-RULES.md](01-RULES.md) §2. It must appear in the new repository's root instructions and entry skills, enforced by its verification script.

## Decisions (D)

Source: user answers, 2026-10-13. Items marked *interpretation* should be confirmed the first time they matter.

| ID | Decision | Affects |
| --- | --- | --- |
| D-01 | The new furnace controller is the **sole brain and sole authority** for machine operation. Every responsibility the Nextion component carries today moves to the controller: program model, validation, program library, run state, operating hours, settings, time handling. The new display is only a **remote with a graphical interface**. | [03](03-LEGACY-SYSTEM-BEHAVIOUR.md) §6, F-076, R1/R2/R21 |
| D-02 | Controller and display communicate over a **custom protocol designed for both** in the new project. | T-02 |
| D-03 | The operator interface runs on the **new HMI board**. The **program library is stored in the controller's memory** (*interpretation* of "programs will be on this board memory"). | PERS-01/03, API-01 |
| D-04 | The new controller board has **16 MB flash**. The old board's flash is unknown to the team; the field `sdkconfig` claims 8 MB, which is unverified. | T-12, F-067 |
| D-05 | The PID values (per furnace) **must be saved**. The field-tuned values are recorded as a note ([04](04-CONTROL-ALGORITHMS.md) §7), not carried over as configuration (D-15). The storage form is open (T-03, T-16). | [04](04-CONTROL-ALGORITHMS.md) §7, T-03, D-15 |
| D-06 | F-057 is accepted as a bug (minor). New rule: **during COOLING the over-temperature/overshoot logic must never end the program.** It detects a temperature that rises or stays flat during cooling, and signals an error if the temperature does not fall for a long time. Parameters are open (T-04). | GUARD-02, F-057 |
| D-07 | **Sensor problems are a top priority**; field errors have already come from sensor loss. A sensor showing an obviously faulty value (typically one that differs drastically from the others) must be **detected, reported to the user, and excluded** from the aggregate. This replaces the current "disagreement is only a warning" policy (F-004) for the new project. Algorithm and thresholds are open (T-05). | TEMP-04/05, F-004, F-001, F-054, F-059 |
| D-08 | The heater coming back on after a shutoff is **unacceptable**. It must be prevented by design and proven by test. | SAFE-03/04, F-062, F-063, R3/R4 |
| D-09 | Manual mode will **hold the target temperature until the operator stops it**. The whole manual process will be revised to be more robust (T-06). | MAN-01/02, F-064, F-065 |
| D-10 | The new project will **likely use custom thermal sensors** instead of the MS9024. The MS9024 repair defect (F-066) is not being fixed. [05](05-SENSING.md) §3 is historical except for its lessons (freshness, validity, read-only diagnostics, per-unit calibration). | T-07, F-066 |
| D-11 | The precision inventory ([10](10-CONFIGURATION-AND-PRECISION.md) §2) is the accepted basis. The concrete values are open (T-08). | T-08 |
| D-12 | The fan is **both the cooler and the heat distributor**. Refined 2026-10-13: the fan is **on during HEATING, HOLDING and COOLING**. Outside those phases it follows two temperature thresholds about **3 °C apart** around x ≈ 40 °C (on above the upper one, off below the lower one), so it keeps running while the chamber is hot. Exact thresholds and RPM control are open (T-09). | FAN-01…03 |
| D-13 | Status, notification and error presentation will be **redesigned together with the new display**. Display-side handling of F-058/F-059-type problems is postponed to that work (T-01). | T-01 |
| D-14 | **Integration-pending rule.** The product owner will supply the new board's MCU and schematic later. Until then, and likewise for the HMI protocol commands, the sensors, logs, cure programs, configuration values and any other unknown interface, development proceeds **without that information**. The code is structured against interfaces, and the gap is marked with `TODO(INTEGRATION T-xx)` and a fail-safe stub, without inventing the missing facts ([01](01-RULES.md) §3). | All platform code, T-02, T-07, T-12, T-13, T-15 |
| D-15 | **Starter folder purpose.** This folder guides agents and engineers: what the old project did (its final, field-used behavior), the lessons, the setup needed and the rules to follow. The old project is **not copied**. Documents tied to the old repository are reproduced as text. Field tuning is recorded as a note only. The legacy skills and agents are included read-only as source material for authoring new ones; they are not copy-pasted or modified. | Whole starter, [08](08-SETUP-FOR-NEW-REPO.md) |

## Deferred topics (T)

| ID | Status | Topic | Trigger areas | Already decided | Ask the user |
| --- | --- | --- | --- | --- | --- |
| T-01 | deferred | Operator status, notifications and error presentation | display/HMI, status model, events/notifications, fault reporting, alarms, operator messages, run outcomes | D-01, D-13. Evidence F-058, F-059, F-014 | Which states, alarms and outcomes must the display show? Acknowledgement rules? Severity levels? Should the controller still distinguish COMPLETED / STOPPED / ABORTED(cause) internally (recommended, R8)? |
| T-02 | deferred | Controller ↔ display communication protocol | protocol, comms, transport (UART/RS-485/CAN/Ethernet/BLE), message schema, versioning, program upload/download, telemetry rate | D-01, D-02, D-03 | Physical link? Message format? Who initiates? Versioning and compatibility? Behavior when the display is disconnected (the controller must keep running safely)? Authentication of commands? |
| T-03 | deferred | How PID values are stored and edited | PID, tuning, feedforward, per-furnace configuration, settings storage | D-05; field values noted in [04](04-CONTROL-ALGORITHMS.md) §7 | Fixed per furnace variant at build time, or persisted settings on the controller? Editable from the display or service console? Who may change them? Per-furnace identity and migration? |
| T-04 | deferred | Cooling-phase guard | cooling stage, overshoot/over-temperature guard, profile guards | D-06; F-057 | What counts as "not falling" (slope threshold, window length)? Alarm only, or pause? Is a rising temperature with the heater off a separate stuck-heater/contactor fault that opens the contactor (the absolute limit T-10 still applies)? |
| T-05 | deferred | Faulty-sensor detection and sensor-loss policy | sensors, aggregation, quorum, validity/freshness, outlier detection, sensor-loss pause and recovery | D-07, D-08; F-001, F-004, F-054, F-059, F-060 | Deviation rule (from median? absolute °C or relative?), minimum sensors for a decision, how long a sensor stays excluded and how it is re-included, notification rules, quorum after exclusions; after loss, auto-resume or operator acknowledgement, and should stage time be extended? |
| T-06 | deferred | Manual mode redesign | manual mode, live target change, manual ramp | D-09; F-064, F-065 | Limits (time, temperature)? Ramp behavior? What happens on sensor loss or fault in manual? Can a program be switched to manual or the reverse? |
| T-07 | deferred | Temperature sensor hardware and interface | sensor driver, bus/Modbus, sensor protocol, calibration, commissioning | D-10 | Sensor type and count? Bus and protocol? Accuracy and resolution? Calibration storage? Per-sensor identity and placement? |
| T-08 | deferred | Precision values | data types, units, program model fields, tolerances, PID period, SSR window, time base | D-11 | Which quantities change, and to what resolution? |
| T-09 | deferred | Fan speed control, thresholds and fan failure | fan driver, airflow, fan states, fan thresholds | D-12 | Exact x and the two thresholds (field build used 43/41 °C; Kconfig default 43/40)? Fan state while PAUSED or after a fault/abort (the phase rule does not cover them)? RPM control needed? Fan failure detection (tach/airflow proof), and does heating stop on fan failure? |
| T-10 | deferred | Absolute temperature limit and hardware cutoff | over-temperature supervisor, safety limits, contactor emergency off | F-077, F-043 | Absolute limit value(s) per furnace? Response time? Is there an independent hard-wired cutoff? |
| T-11 | deferred | Completion threshold and end of cooldown | cooldown, program completion | — | Fixed 40 °C, configurable, or operator-confirmed? What if ambient keeps the chamber above the threshold? |
| T-12 | deferred | Target MCU and flash layout | board, devicetree, partitions, storage layout | D-04 (16 MB) | Which MCU/board? Partition plan for 16 MB (app slots/OTA, settings, program library, logs/coredump)? |
| T-13 | deferred | Migration of existing field data | persistence, import, program format, operating hours | D-01, D-03, D-05 | Import existing `.prg` programs from the panel SD cards? Carry over operating hours from the old NVS? Per-furnace PID values? |
| T-14 | deferred | Heater actuation scheme | SSR window, PWM, contactor sequencing, demand latency | F-078, D-08 | Window length? Mid-window demand updates? SSR type (zero-cross minimum on-time)? Accept re-tuning? |
| T-15 | deferred | Output polarity and reset behavior | GPIO/outputs, boot/reset state, watchdog response, brownout | F-042, F-047 | Schematic polarity and pulls of the new board? Required output state during reset, brownout and watchdog? |
| T-16 | deferred | Confirm two recorded interpretations | program storage location, PID storage | D-03, D-05 | D-03: are programs stored in the **controller's** memory (not the display's)? D-05: what exactly does "save the PID values" require (carry the values over, persist them on the controller, make them editable)? Ask when T-02, T-03 or T-13 work starts. |
| T-17 | deferred | Agent tooling for the new repository | instruction files, skills, agents, repository verification | D-15 | Codex, Claude Code, both, or other? That decides file names and discovery paths (`AGENTS.md`, `CLAUDE.md`, skill and agent locations). Ask before creating any instruction file. |

## Change log

- 2026-10-13: register created from the user's answers to the handoff open questions (Q-01…Q-09).
- 2026-10-13: D-12 refined (fan rule by phase plus hysteresis); T-16 added (the user will confirm both interpretations later).
- 2026-10-13: D-14 (integration-pending rule), D-15 (starter purpose) and T-17 (agent tooling) added when the starter kit was built.

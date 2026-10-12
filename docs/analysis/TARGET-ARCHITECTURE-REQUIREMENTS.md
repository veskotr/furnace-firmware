# Target architecture requirements

Status: discovery record; accepted decisions are authoritative only where linked
to an ADR. Nothing in this note is implemented by the current firmware.

## Current delivery scope (2026-09-14)

The user has prioritized analysis/documentation and subsequent Zephyr preparation
for the central ESP32 controller, with an external HMI and a control/status
protocol. The other planned hardware, including the STM32 actuator controller,
is deferred. See [central-controller preparation](ESP32-ZEPHYR-PREPARATION.md)
for scope, source-backed HMI separation impacts, and remaining questions.

The requirements and ADRs below retain the broader future direction; they do
not all belong to the first controller implementation. Current ESP32 output
ownership remains the analysis baseline until a validated hardware migration.

The user confirmed real-oven use of the ESP-IDF firmware and prioritized safety,
error handling, race elimination, memory, logging, temperature processing, and
structure. The preparation note records proposed acceptance criteria; exact
deployed revision/configuration and quantitative cure limits remain open.

The user also confirmed automatic resume after temporary sensor-fault recovery
once data is trustworthy (`POL-SENSOR-RECOVERY-001` in the preparation note).
Qualification timing/counts and interaction with other pause/inhibit reasons
still require specification; this does not settle recovery for other faults.

The operating oven currently has no independent hardware safety protections.
ESP32 operational checks are not certified safety functions. The production
version is intended to add independent hardware safety mechanisms
(`POL-HARDWARE-SAFETY-001` in the preparation note); no present hardware cutoff
or interlock can be credited by this source analysis.

## Confirmed product direction

- One ESP32 firmware family should support multiple heating-product variants.
- Provisioning writes a versioned configuration file to 8 MiB ESP flash; the
  configured device graph is loaded at boot and remains fixed during operation.
- Factory safety ceilings remain compiled/non-overridable. Later remote support
  configuration and signed OTA require explicit on-site operator confirmation
  and an enforced non-running safe state.
- A furnace uses up to 16 Modbus RTU temperature transmitters. Old and custom
  transmitter families are not expected to coexist in one installation.
- Custom STM32 transmitters can support faster baud, broadcast capture, sequence
  identity, and later alarms/status. All configured sensors participate in the
  product-defined temperature policy; the aggregation algorithm remains open.
- The future custom HMI uses a structured full-duplex RS-422 protocol. A running
  program continues if the HMI is lost. Network access initially permits status,
  logs, STOP, and pause, but not start or resume.
- The ESP32 coordinator owns furnace process state, program execution, PID, and
  command authorization. Components own their hardware/state and publish typed,
  coherent snapshots.
- After HMI externalization, the furnace controller owns cure-program
  management/validation and receives operator commands through the HMI
  (`POL-PROGRAM-OWNERSHIP-001`); the HMI does not own active-run authority.
  Persistence format and service-hour semantics remain undecided, with service
  hours explicitly deferred from the first contract.
- [ADR-0003](../decisions/0003-stm32-actuator-authority.md) assigns final
  actuator/interlock authority and SSR windowing to a separate STM32 controller.
- [ADR-0004](../decisions/0004-actuator-session-sequence-and-stop.md) makes
  positive actuator control session/sequence/lease checked while global STOP is
  unconditional and idempotent.
- Critical failures should retain a structured state snapshot plus recent
  history in LittleFS where possible; panic/power-loss completeness is a
  best-effort layered requirement, not yet guaranteed.

## Target contract set

- [Board revision two system architecture](../architecture/BOARD-REV2-SYSTEM-ARCHITECTURE.md)
- [Revision-two component contract standard](../architecture/COMPONENT-CONTRACT-STANDARD.md)
- [Coordinator state and command model](../architecture/COORDINATOR-STATE-AND-COMMAND-MODEL.md)
- [ESP32–STM32 actuator protocol requirements](../architecture/ESP-STM32-ACTUATOR-PROTOCOL-REQUIREMENTS.md)

These are design contracts, not descriptions of the current implementation.

## Current-source gaps against the target

- Limits conflict: device manager allows 32 entries, temperature-device pool 16,
  processor array 9, and `app_main` creates 5.
- Current sensor polling inserts an unconditional 100 ms per device and uses a
  300 ms Modbus response timeout, so scheduling must be redesigned before the
  configured maximum can meet a bounded batch deadline.
- Current temperature transfer uses mutable cache fields and separate value and
  validity events (F-060); the target requires an immutable sequenced batch.
- Current dispatcher exposes both coordinator and heater targets. The target
  accepts external furnace intent only through coordinator authorization.
- Current HMI owns domain/persistence state that must move behind HMI-independent
  program/configuration services.
- Current ESP32 directly owns SSR/contactors and the 5 s SSR window. That remains
  authoritative until ADR-0003 migration and hardware validation are complete.
- The tracked configuration declares 2 MiB single-app flash, while deployed
  hardware is reported as 8 MiB and future OTA requires two app slots plus
  program/config/log storage (F-068/P-010).
- Current crash retention is an incomplete RTC/log-ring seed; structured furnace
  and actuator snapshots, actual panic coverage, and power-loss behavior remain
  unverified.

## Open requirements

- Product-specific temperature aggregation/quorum, maximum batch deadline, and
  acceptable scan skew.
- Fan behavior after airflow loss, E-stop, contactor/current mismatch, and other
  non-communication fault classes. ESP32 lease expiry turns off every output.
- Exact STM32 lease/refresh timing; 2–3 seconds to communication-loss shutdown is
  provisional.
- Which transient/recoverable faults warn, auto-recover, require authenticated
  clear, or remain locally latched.
- Reset/program checkpoint policy by failure class and maximum resumable outage.
- Critical-snapshot survival target: software reset/panic versus complete power
  loss.
- Status LED meanings and physical pin assignment.

## Remaining decisions

1. Versioned boot-configuration schema, integrity, A/B activation, factory-limit
   validation, and service/OTA authorization.
2. Temperature bus scheduler, transmitter driver contract, coherent batch, and
   product aggregation policies.
3. Pause, reset/recovery, and detailed coordinator fault-state policies within
   the documented command/state boundary.
4. STM32 Modbus register encoding, exact timing, session handshake, and fault
   taxonomy within ADR-0003/ADR-0004.
5. Eight-MiB OTA/LittleFS/NVS partition contract and structured fault-record
   format.
6. Reset, recovery-paused, local resume, and fan-after-fault safety policies.

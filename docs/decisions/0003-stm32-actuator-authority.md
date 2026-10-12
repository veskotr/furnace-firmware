# ADR-0003: STM32 owns actuator safety and SSR windowing

- **Status:** accepted
- **Date:** 2026-08-04

## Context

The current ESP32 firmware computes PID demand and implements a configurable
time-proportioning SSR window in the heater task. The target product family is
expected to use up to 16 fixed, boot-configured Modbus RTU temperature
transmitters, a replacement structured-command HMI, and later network/service
adapters. A planned STM32-based external controller will operate and monitor
the fan, airflow, SSR, main/heater contactors, auxiliary contacts, and rough
current state.

The SSR is zero-crossing. The ESP32 is intended to remain the furnace process
coordinator and program/PID owner while reducing its direct interaction with
physical outputs. ESP32 and STM32 may initially share the Modbus bus with the
temperature transmitters because the current board has no spare UART.

## Problem

The system needs one unambiguous owner for SSR timing and final physical-output
authorization. Sending raw SSR ON/OFF timing across shared Modbus would make a
lost or delayed OFF request, bus congestion, and ESP32 scheduling part of the
physical pulse contract. Fault acknowledgement must also distinguish a request
to reevaluate from an unsafe unconditional clear.

## Constraints

- The ESP32 owns profiles, PID calculation, process state, and operator/network
  command authorization.
- The STM32 owns actuator state, output feedback, local interlocks, SSR timing,
  communication-loss enforcement, and persistent hardware fault records.
- Production demand is a bounded percentage, not raw ON/OFF pulse commands.
- Direct output removal and lease expiry cannot depend on ESP32 event, HMI, or
  logging delivery.
- The airflow switch is planned as a physical interlock. Main-contactor
  auxiliary feedback and rough current monitoring are planned on the STM32.
- Factory safety limits cannot be weakened by provisioned configuration.
- Communication-loss shutdown is provisionally expected within 2–3 seconds and
  turns off every output; the exact lease and refresh budget requires protocol
  and bench evidence.
- Fan behavior after non-communication fault classes is unresolved and must be
  decided with the hardware/team safety review.

## Considered options

1. Keep SSR windowing on the ESP32 and send raw ON/OFF commands to the STM32.
2. Keep PID/profile calculation on the ESP32; send bounded demand inside an
   expiring STM32-owned control session; let the STM32 generate the SSR window
   and enforce physical safety.
3. Move PID, profiles, SSR windowing, and complete process control to the STM32.

## Decision

Choose option 2.

The ESP32 sends versioned, sequenced requested demand within an STM32-owned
control session. The STM32 applies that demand using its factory-bounded local
SSR window only while the session lease and hardware preconditions remain
valid. Zero demand, STOP, a local safety fault, or lease expiry removes
SSR/heater authority immediately, including during an active window.

[ADR-0004](0004-actuator-session-sequence-and-stop.md) defines session,
sequence, duplicate, expiry, and unconditional STOP semantics. Authorization is
not a separately stored wire Boolean.

Production protocol does not support raw SSR ON/OFF timing commands. Any
manufacturing-only output exercise must use a separately authorized test
contract that cannot be enabled accidentally in production.

STM32 faults are classified rather than treated uniformly:

- A transient event that has physically recovered is retained in history and
  may produce a warning according to its fault policy.
- A recoverable fault can be reevaluated after an authenticated/authorized ESP32
  clear request; the STM32 clears it only after verifying its physical recovery
  conditions.
- An unrecoverable or still-present fault remains latched and output-inhibiting.

The STM32 retains ECU-style fault history across its own reset. The ESP32
maintains the furnace-level state and structured fault snapshot separately.

## Rationale

Local windowing is deterministic and independent of shared-bus jitter. It makes
the component that measures fan, airflow, contactor, current, and SSR state the
same component that authorizes physical output. An expiring lease makes bus or
ESP32 loss fail off without requiring delivery of a final STOP command.

Moving the complete PID/profile controller to the STM32 would duplicate furnace
domain behavior and weaken the intended coordinator role. Raw ON/OFF commands
would recreate remote timing on a bus and require a second local expiry
mechanism, which is effectively local window ownership with a worse interface.

## Consequences

- The future heater interface becomes requested demand within a sequenced,
  expiring control session plus fault-clear requests; it is not a remote GPIO
  API.
- STM32 feedback becomes an immutable snapshot of requested/actual fan,
  airflow, SSR, contactor, current, lease, and fault state.
- The Modbus scheduler must prioritize STOP, lease refresh, and actuator
  feedback above temperature polling and diagnostics.
- A bus jam can stop heating through lease expiry even when the ESP32 remains
  healthy. This is a safe availability failure.
- Window length and pulse constraints reside in factory-bounded STM32
  configuration. The ESP32 may report them but does not time each pulse.
- Current ESP32 GPIO/window behavior remains the implemented architecture until
  the new controller and protocol pass software and hardware validation.
- Fan behavior after non-communication faults is deliberately not decided by
  this ADR; lease expiry turns off every output.
- The fault protocol and persisted records require explicit versioning and
  migration rules before deployment.

## Migration plan

1. Specify the versioned Modbus request, state-snapshot, lease, sequence/session,
   fault-class, fault-history, and authorized-clear contracts.
2. Implement and unit-test the STM32 state machine with outputs disconnected.
3. Add an ESP32 actuator-controller adapter behind a typed interface while
   retaining the current local heater implementation as a characterized
   migration baseline.
4. Run shared-bus scheduling/fault injection with the configured sensor count,
   including timeouts, corrupted replies, bus silence, stale sessions, and
   priority STOP/lease traffic.
5. Perform authorized powered-controller validation of zero-cross windowing,
   interlocks, contactor/current feedback, lease expiry, and reset states.
6. Change production output ownership only after the external controller is the
   verified physical authority; then remove production raw ON/OFF commands and
   obsolete ESP32 GPIO ownership in a separate reviewed change.

## Rollback strategy

During migration, select exactly one output authority per build/hardware setup.
Rollback may select the characterized local ESP32 heater implementation only on
hardware designed for it and only with its existing direct-inhibit protections.
Never run both controllers as competing output owners. Do not roll back a
released external-controller safety function without an equivalent verified
off/lease/interlock mechanism.

## Safety implications

The actuator boundary must default off at boot, reset, invalid configuration,
invalid/stale session, communication loss, local watchdog failure, airflow loss,
and feedback mismatch. An ESP32 fault-clear command is only a request to verify
recovery. Logging, event delivery, or command acknowledgement never substitutes
for physical output removal.

Fan behavior after heater removal for non-communication faults remains an open
safety requirement and is a release blocker for the final fault-response table.
Lease expiry turns off every output. Exact GPIO polarity,
electrical interlocks, lease timing, common power/bus failure, and real
de-energization latency require schematic and powered-controller evidence.

## Test implications

- Unit-test percentage clamping, zero demand, window boundaries, demand changes,
  zero-cross output timing, and immediate inhibit during an active window.
- Test session/sequence replay, malformed messages, missed refreshes, lease
  expiry, ESP32/STM32 reboot order, and stale demand rejection.
- Test transient, recoverable, persistent, and unrecoverable fault transitions;
  unauthorized clear; authorized reevaluation; and refusal to clear a
  still-present physical mismatch.
- Test persistent STM32 fault history and correlation with the ESP32 structured
  critical snapshot.
- Stress the shared bus with all configured sensors, maximum permitted response
  times, one jammed/offline transmitter, and immediate STOP traffic.
- Bench-validate SSR/contactors/fan/airflow/current feedback and measured
  time-to-off. No source, mock, or compilation result proves those properties.

# ADR-0004: Actuator session, sequence, lease, and unconditional STOP

- **Status:** accepted
- **Date:** 2026-08-04

## Context

[ADR-0003](0003-stm32-actuator-authority.md) makes the future STM32 controller
the authority for every SSR/contactor output, local feedback/interlocks, and SSR
windowing. ESP32 supplies bounded demand across a shared Modbus RTU bus. Delayed,
duplicated, buffered, or retried commands must not restore heat after STOP,
communication loss, or controller reset.

The initial description separated duty from an authorization value. A stored or
delayed authorization Boolean is no fresher than stored duty and therefore does
not solve replay or mixed-generation behavior.

## Problem

Define how the STM32 decides that an ESP32 demand is current, how communication
loss expires it, and how STOP behaves when received out of order. The design
must fail off without making a final STOP delivery the only safety mechanism.

## Constraints

- ESP32 is the sole Modbus master; STM32 shares the bus with up to 16 sensors.
- STM32 turns off every output when its control heartbeat/lease expires.
- A stale command may remove energy but must never add or maintain energy.
- An out-of-order or duplicate STOP must be respected.
- Modbus CRC detects corruption, not replay or semantic freshness.
- Wire/register encoding and final timeout values are not yet selected.

## Considered options

1. Transmit duty plus an authorization Boolean and use the most recently stored
   values. This leaves both fields stale together and does not reject replay.
2. Accept the latest duty while a bare heartbeat is present. A duplicated
   heartbeat can incorrectly extend authority and neither reset nor ordering is
   represented.
3. Use an STM32-owned control session with non-repeating identity, strictly
   increasing control sequence, and a local expiring lease; treat global STOP
   as an unconditional idempotent command.
4. Require every command, including STOP, to have a newer sequence. This is
   orderly but can reject a delayed safety-removal request.

## Decision

Choose option 3.

Opening a heating session establishes a non-repeating identity and resets its
control sequence. Only a complete valid message for the active session with a
strictly newer sequence may change duty or renew the local STM32 lease. An exact
duplicate may receive the same acknowledgement but does not renew the lease.
A lower sequence, wrong session, or same sequence with different content is
rejected and recorded.

Global all-output-off is applied before session/sequence freshness checks. It is
idempotent and closes the active session even when the received command is old,
duplicated, or belongs to stale context. Once closed, no later command from that
session can restore an output, regardless of its numeric sequence. A new local
START flow must establish a new session.

Authorization is therefore STM32-owned session/lease state, not an independent
wire Boolean. A duty value is usable only inside the active, unexpired session
and while all local hardware conditions permit it.

## Rationale

Session identity separates runs and controller generations. Sequence identity
rejects delayed or reordered positive demand within a run. Local lease expiry
handles total bus/ESP32 loss. Unconditional STOP deliberately favors physical
safety over availability: replay may interrupt a run, but it cannot energize or
keep an output energized.

## Consequences

- Every control message carries protocol version, session identity, sequence,
  complete payload, and exact length as one atomic application unit.
- Session identity must not repeat across relevant resets; the exact challenge,
  nonce, or persisted-generation mechanism is still to be decided.
- Retransmission behavior must distinguish duplicate acknowledgement from lease
  renewal.
- The bus scheduler prioritizes STOP, session, lease, and actuator-state traffic.
- Old STOP replay is a diagnosable safe availability failure and potential
  denial-of-service concern; access control and queue hygiene still matter.
- Query/correlation identifiers are separate from the control sequence.
- Fault clear is freshness/session checked and never receives STOP's unconditional
  treatment.

## Migration plan

1. Specify and review the semantic protocol before assigning registers.
2. Implement an STM32 state-machine model with outputs disconnected.
3. Test reset generations, duplicate/lower messages, exact duplicate replies,
   mismatched duplicates, lease expiry, unconditional STOP, and late positive
   demand after STOP.
4. Add the ESP32 client and prioritized shared-bus scheduler behind a typed
   actuator interface.
5. Validate with maximum configured sensor load before moving physical outputs.
6. Migrate exactly one actuator authority per hardware/build configuration.

## Rollback strategy

The protocol may be revised before deployment by incrementing its incompatible
version and retaining no cross-version active session. Rollback to the current
ESP32 actuator implementation is permitted only on compatible hardware and with
its characterized direct-inhibit behavior. Never retain an active session across
firmware rollback or run ESP32 and STM32 as competing output owners.

## Safety implications

Only fresh positive-control traffic can add or maintain output authorization;
stale STOP traffic may always remove it. Lease expiry, controller reset, invalid
session, and malformed control messages fail off. E-stop, airflow, output
feedback, current, and other local interlocks remain STM32-enforced and cannot
be bypassed through session establishment or local service commands.

## Test implications

- Exhaustively test the command-ordering table in
  [the protocol requirements](../architecture/ESP-STM32-ACTUATOR-PROTOCOL-REQUIREMENTS.md).
- Verify that duplicates do not extend the lease and that a stopped session
  rejects a later higher-sequence demand.
- Measure worst-case shared-bus latency and physical time-to-off under dropped,
  delayed, corrupted, and nonresponsive-slave conditions.
- Test replayed STOP as both a safety success and an availability/security event.
- Hardware validation is required for actual outputs, interlocks, and reset
  behavior; source tests cannot establish them.

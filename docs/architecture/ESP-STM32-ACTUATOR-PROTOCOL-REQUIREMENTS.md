# ESP32–STM32 actuator protocol requirements

Status: semantic target contract; wire/register layout is intentionally TBD.
Date: 2026-08-04

This document refines [ADR-0003](../decisions/0003-stm32-actuator-authority.md)
and [ADR-0004](../decisions/0004-actuator-session-sequence-and-stop.md).
It defines what the shared Modbus RTU protocol must prove before physical-output
ownership migrates from the current ESP32 implementation.

## Roles and physical scope

- ESP32 is the sole Modbus RTU master.
- STM32 actuator controller is a slave sharing the bus with up to 16 configured
  temperature transmitters.
- STM32 commands every contactor and SSR in the furnace.
- STM32 monitors each available SSR/contactor auxiliary contact, the E-stop,
  airflow switch, and configured phase-current transformer loops.
- Current transformers may cover the combined fan/heater feeds or individual
  outputs depending on product cost/configuration.
- STM32 owns zero-cross SSR windowing, local interlocks, output mismatch
  detection, communication-loss response, and persistent hardware fault history.

## Safety invariants

1. STM32 boot/reset, invalid configuration, watchdog failure, expired control
   lease, E-stop, and applicable local interlock failure turn off every output.
2. No stored duty value is sufficient to energize an output.
3. Only a fresh command in the active control session may renew the lease or
   update duty.
4. Global all-output-off is idempotent and is applied even when its session or
   sequence is old.
5. STOP or lease expiry closes the current control session; later messages from
   that session cannot restore output even if they carry a higher sequence.
6. Physical feedback mismatch is evaluated locally. Reporting it to the ESP32
   is not the mitigation.
7. Direct production SSR ON/OFF pulse commands are not supported.

## Heating session and lease

Authorization is not a transmitted Boolean. It is STM32-owned state derived
from a deliberately opened control session, fresh sequenced messages, and valid
local interlocks.

A session has:

- a non-repeating identifier across relevant ESP32/STM32 resets;
- an active/closed state owned by STM32;
- the highest accepted control sequence;
- a monotonic lease deadline owned by STM32;
- bounded requested duty;
- the physical/interlock conditions required to apply that duty.

The exact session handshake is TBD. It must prevent an old buffered request
from becoming valid after either controller resets. Candidate mechanisms include
an STM32 boot challenge combined with an ESP32 nonce or a persisted generation;
a constant boot-time session ID is unacceptable.

Every strictly newer accepted control/renewal message advances the sequence and
renews the lease. Repeated or older messages do not renew it. The STM32 may
acknowledge an exact duplicate so a Modbus retry can finish, but it does not
reapply the command or extend heating authority.

## Ordering rules

| Operation | Old/wrong session | Lower sequence | Exact duplicate |
| --- | --- | --- | --- |
| Global all-output-off | Apply and report stale context | Apply | Apply idempotently |
| Duty/lease renewal | Reject | Reject | Acknowledge; do not renew lease |
| Open new session | Validate as a new handshake | Not applicable | Idempotent result when safely identifiable |
| Fault-clear request | Reject | Reject | Return prior result; do not repeat clear side effects |
| Snapshot/history query | Permit under query contract | Not a control sequence | Permit |

The same session/sequence with different payload content is a protocol fault and
is never applied. Control sequence width should make wrap irrelevant for the
maximum session lifetime; 64 bits is the current recommendation.

### Out-of-order STOP example

```text
SET_DUTY session=42 sequence=100 duty=700
SET_DUTY session=42 sequence=101 duty=650
ALL_OFF  session=42 sequence=90
SET_DUTY session=42 sequence=102 duty=800
```

`ALL_OFF` is applied despite its lower sequence and closes session 42. The later
sequence 102 is rejected because a closed session cannot be revived. An old or
replayed global STOP can therefore cause a safe availability interruption; it
cannot energize output. The event is retained for diagnosis.

## Atomic command application

A logical control request is transferred and applied as one validated unit. A
multi-register representation must use one complete Modbus operation (for
example function `0x10`) or an explicit staging-and-commit protocol. The STM32
must not act on a partly updated register set.

The Modbus frame CRC detects transmission corruption but does not provide
freshness, replay protection, application-level compatibility, or network
authorization. Session, sequence, message version, range validation, and exact
length remain application responsibilities.

## Control operations

The semantic protocol needs at least:

- negotiate/read protocol and capability version;
- read actuator identity and boot/session challenge;
- open a heating-control session;
- set bounded duty and renew the active lease;
- global all-output-off;
- read one coherent actuator snapshot;
- read fault/DTC history;
- request fault reevaluation/clear;
- close a session without energizing output.

Manufacturing-only individual-output exercise is outside the production
contract. If later required, it needs an explicit local service mode, separate
build/provisioning safeguards, and cannot bypass local hardware interlocks.

## Timing and bus scheduling

- Lease measurement and expiry are local STM32 monotonic-time operations.
- The refresh period must allow several failed/retried transactions before the
  lease expires.
- The bus scheduler prioritizes global STOP, session traffic, lease refresh,
  and actuator safety snapshots above sensor polling and diagnostics.
- A slow or failed temperature transmitter cannot monopolize the bus until the
  actuator lease accidentally expires.
- The 2–3 second communication-loss shutdown range is a provisional design
  target. No refresh period has been selected; final values require worst-case
  transaction analysis and shared-bus bench measurements.
- If the valid refresh deadline is missed, every output turns off. Fan behavior
  after other fault classes requires a separate hardware and product-policy
  decision.

## Actuator snapshot

One immutable, versioned response should include:

- STM32 identity, firmware/protocol version, boot identity, and snapshot sequence;
- active/closed session identity and remaining/expired lease state;
- requested duty and locally applied duty/window state;
- commanded and observed state for every configured SSR and contactor;
- E-stop and airflow state;
- configured current-loop measurements and validity;
- active faults, latch class, first/last occurrence, and occurrence count;
- reset/watchdog reason and configuration identity;
- timestamp/age information sufficient for the ESP32 to reject a stale snapshot.

Unsupported or unconfigured channels are represented explicitly rather than as
plausible zero measurements.

## Fault clear boundary

Fault-clear details remain TBD, but these invariants already apply:

- ESP32 sends a request to reevaluate; it does not overwrite physical truth.
- STM32 verifies recovery conditions before clearing an active latch.
- Clearing an active latch and erasing retained history are different actions.
- A clear request uses the active session/freshness rules appropriate to its
  privilege and cannot be replayed as a new request.

## Validation requirements

- Protocol-unit tests: versions, sizes, ranges, atomic commit, duplicates,
  lower/higher sequences, mismatched payload duplicates, and session closure.
- Reset tests: every ESP32/STM32 boot order, retained/buffered frames, and new
  session establishment.
- Timing tests: dropped replies, busy bus, maximum configured sensors, one
  nonresponsive slave, and measured lease expiry.
- Safety tests: out-of-order/replayed STOP, late positive duty after STOP,
  E-stop, airflow, auxiliary mismatch, current mismatch, and local watchdog.
- Powered-controller validation: actual output polarity, zero-cross timing,
  feedback, reset state, and time-to-off. Compilation and mocks are not proof.

## Explicitly unresolved

- Register/function layout and byte order.
- Session nonce/challenge construction and retained-reset behavior.
- Final refresh, timeout, retry, and scan budgets.
- Reconnect behavior above the required fresh-session boundary.
- Fault taxonomy, authorized-clear roles, history capacity, and retention format.
- Fan response after each loss/fault condition.
- Per-product current-transformer topology and mismatch thresholds.

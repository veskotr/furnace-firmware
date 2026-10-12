# Board revision two system architecture

Status: target contract; not implemented by the current firmware.
Date: 2026-08-04

This document defines durable system boundaries for the planned second board
revision. It does not select GPIOs, register addresses, final timing values, or
product-specific fault responses. The current implementation remains described
by [the repository map](../codex/REPOSITORY_MAP.md).

## Objectives

- Reuse one ESP32 firmware family across multiple heating products using a
  fixed, boot-loaded device configuration.
- Keep furnace process logic independent of HMI and network transports.
- Give every physical output and mutable state one authority.
- Make communication loss and component failure remove output energy without
  relying on delivery of a final software command.
- Keep the two-person development model understandable: typed component APIs,
  bounded asynchronous operations, and coherent snapshots.

## System context

```text
                     local commands/status
  Custom HMI  <================================>  ESP32 coordinator
   RS-422                   full duplex              |
                                                    | sole Modbus master
  Network  <----- restricted commands/status -------+-------- RS-485 bus
  UART0    <----- local technician service ----------+          |
                                                               +-- 1..16 temperature transmitters
                                                               |
                                                               +-- STM32 actuator controller
                                                                    |
                                                                    +-- every SSR and contactor output
                                                                    +-- SSR/contactor auxiliary feedback
                                                                    +-- E-stop state
                                                                    +-- airflow switch
                                                                    +-- phase-current transformers, as fitted
```

The STM32 actuator controller and temperature transmitters initially share one
Modbus RTU bus. The ESP32 is the sole bus master. Actuator STOP, lease renewal,
and safety-state traffic takes scheduling priority over temperature acquisition
and diagnostics.

## Ownership

| Concern | Authority | Contract |
| --- | --- | --- |
| Furnace operating state | ESP32 coordinator | Accepts authorized furnace intent and owns the single active execution. |
| Program files | ESP32 program manager | Loads/stores versioned programs through the filesystem and supplies a validated program to the coordinator. |
| Profile progression and PID | ESP32 coordinator/control logic | Produces bounded requested duty; never times individual SSR pulses. |
| Modbus RTU scheduling | ESP32 Modbus master | Serializes all transactions and prioritizes actuator safety traffic. |
| Temperature-device protocol | Typed transmitter instances | Convert protocol data into immutable, valid/fresh samples. |
| Physical output authority | STM32 actuator controller | Operates every SSR and contactor and owns the local output state machine. |
| SSR zero-cross window | STM32 actuator controller | Applies bounded ESP32 demand under a live control session and local interlocks. |
| Physical feedback | STM32 actuator controller | Monitors available SSR/contactor auxiliary contacts, E-stop, airflow, and fitted current transformers. |
| HMI presentation | Custom HMI adapter | Translates structured local commands and renders snapshots; owns no furnace-domain state. |
| Network presentation | Network adapters | Expose the restricted remote command set and read-only data. |
| Boot configuration | ESP32 configuration service | Validates product/device configuration against immutable factory ceilings before component start. |
| Fault history | STM32 and ESP32, at their respective layers | STM32 retains hardware DTC-like history; ESP32 retains furnace/process snapshots and correlation data. |

There is no universal device-manager authority in the target design. A small
registry may retain configured identity, type, capability, and health, but bus
ownership stays with the transport scheduler and furnace policy stays with the
coordinator. HMI and network adapters are not furnace devices.

## Runtime topology

- Components may allocate their contexts during boot.
- The configured graph is fixed after successful boot; routine dynamic
  add/remove/restart is not required.
- Runtime messages and samples use bounded queues and static buffers.
- A fatal component failure may cause a controlled safe-state restart, but no
  general hot-restart framework is required.
- Up to 16 temperature transmitters participate in the configured product
  temperature policy. Old and custom transmitter families are not expected to
  coexist in one installation.

## Command boundaries

- Local HMI may start, stop, pause, resume, and query the furnace.
- Remote interfaces may query status/logs and request STOP or pause. They may
  not start or resume heating.
- A technician physically connected to UART0 may use the broad service command
  set. Service commands still cannot bypass STM32-enforced E-stop, airflow,
  feedback, current, or other physical interlocks.
- Loss of HMI does not stop a healthy running program. Loss of the ESP32 control
  lease causes the STM32 to turn off every output.
- Only the coordinator accepts external furnace intent. Transport adapters do
  not address actuator drivers directly.

Detailed command semantics are in
[COORDINATOR-STATE-AND-COMMAND-MODEL.md](COORDINATOR-STATE-AND-COMMAND-MODEL.md).

## Failure boundaries

| Failure | Required boundary response |
| --- | --- |
| ESP32 reset/crash or bus silence | STM32 lease expires and turns off every output. |
| STM32 reset | Outputs default off; no retained request may restore them. |
| HMI loss | Running program continues; loss is reported through other available indicators/interfaces. |
| Network loss | No local-control effect. |
| Temperature invalidity/staleness | ESP32 removes heating demand/ends authorization according to coordinator safety policy. |
| E-stop or local physical interlock failure | STM32 removes affected/all output authorization locally, independent of ESP32 message delivery. |
| Feedback mismatch | STM32 records and handles the hardware fault according to the future fault table. |
| Persistent storage failure | Control remains bounded and safe; unavailable history/config updates are reported and never treated as successful. |

## Configuration and storage direction

- Provisioning writes a versioned configuration to the ESP32's 8 MiB flash.
- Configuration is loaded once at boot and validated before dependent
  components start.
- Factory safety ceilings are compiled, non-overridable limits.
- Future configuration changes and OTA require local operator confirmation and
  a firmware-enforced non-running safe state.
- Program storage belongs behind a program-manager API rather than the HMI.
- Final partition sizes, schema encoding, integrity mechanism, and rollback
  layout remain separate decisions.

## Explicitly unresolved

- Whether pause removes heat and freezes time or holds the current temperature.
- Fan behavior for hardware/furnace fault classes other than ESP32 lease expiry,
  which turns off every output.
- Temperature aggregation/quorum and maximum accepted batch skew.
- Exact Modbus baud, actuator refresh period, lease duration, and register map.
- ESP reset/reconnect and program-checkpoint recovery policy.
- Detailed fault classifications and authorized-clear procedure.
- Current-transformer placement: fan/heater feeds or each individual output.
- Status LED meaning and hardware allocation.

## Migration rule

Current ESP32 GPIO and SSR-window ownership remains active until a separately
reviewed revision-two migration is implemented and validated. During migration,
exactly one controller may own each physical output. No build or wiring state may
permit competing ESP32 and STM32 actuator authority.

See [ADR-0003](../decisions/0003-stm32-actuator-authority.md) and
[ADR-0004](../decisions/0004-actuator-session-sequence-and-stop.md).

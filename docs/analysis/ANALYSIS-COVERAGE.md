# Analysis coverage and ownership index

Date: 2026-09-14  
Baseline: `1fcb4b1115ce9560fe2c4944ebc868cb454e5ed5`, branch
`audit-docs-verification`; see [baseline record](ANALYSIS-BASELINE.md).  
Status: component inventory complete; A–F source reviews and bounded
cross-component challenge/test design integrated. Software correction, test
execution and field/hardware validation are pending.

## Classification and evidence limits

All 24 local component directories and `main` have a primary package in the
[execution plan](ANALYSIS-EXECUTION-PLAN.md). Active means reachable through
current startup or command paths, not observed successful initialization.
Libraries execute in their callers or an external driver context; they do not
necessarily create an application task. Optional-enabled HMI is counted once.

Registered sources, compilation commands, archives offered to the linker, and
retained executable objects are distinct evidence. Stored build metadata
corroborates the source inventory; it is not a new build or deployed-image proof.

## Complete local component inventory

Every row below is **inventory verified**. A–F entries now have source-reviewed
behavior at the package boundary. Source
symbols identify the current boundary, not a completed behavioral specification.
Package A owns outputs, B sensing, C process/control, D concurrency/messaging,
E HMI, and F operations/build; the lead integrates shared contracts.

| Component | Current disposition | Primary package | Source evidence and current placement | Audit home |
| --- | --- | --- | --- | --- |
| `main` | Active runtime | F | [app_main](../../main/main.c) orchestrates startup and remains in its delay loop. | [001](AUDIT-001-baseline-output-safety.md), [008](AUDIT-008-configuration-build.md) |
| `commands_dispatcher` | Active runtime | D | [commands_dispatcher_init](../../components/commands_dispatcher/src/commands_manager_core.c) is called by main; [commands_dispatcher_task](../../components/commands_dispatcher/src/commands_dispatcher_task.c) consumes commands. | [006](AUDIT-006-dispatcher-device-lifecycle.md) |
| `common` | Header-only support, actively used | C | [CMake](../../components/common/CMakeLists.txt) registers includes without sources; [core_types.h](../../components/common/include/core_types.h) defines program/task types. F covers generic utilities, D message use, E HMI/config coupling. | [005](AUDIT-005-pid-profile-control.md), [008](AUDIT-008-configuration-build.md) |
| `coordinator_component` | Active runtime; worker starts per profile | C | [init_coordinator](../../components/coordinator_component/src/coordinator_core.c) is called by main; [start_heating_profile](../../components/coordinator_component/src/coordinator_component_heater_controller.c) creates the control worker. Uses dispatcher, event and timer contexts too. | [002](AUDIT-002-temperature-authorization.md), [005](AUDIT-005-pid-profile-control.md) |
| `debug_console` | Active runtime | F | [debug_console_init](../../components/debug_console/src/debug_console.c) is called by main and creates the UART0 console task. | [007](AUDIT-007-operations-diagnostics.md) |
| `device_manager` | Active runtime | B | [device_manager_init](../../components/device_manager/src/device_manager_core.c) is called by main; [device_manager_task](../../components/device_manager/src/device_manager_task.c) updates devices. D reviews shared table lifetime. | [003](AUDIT-003-modbus-device-ownership.md), [006](AUDIT-006-dispatcher-device-lifecycle.md) |
| `error_manager` | Dormant link input | F | Coordinator [CMake](../../components/coordinator_component/CMakeLists.txt) requires it, but no application callers of [register_error_descriptor/get_error_description](../../components/error_manager/src/error_manager.c) were found. | [007](AUDIT-007-operations-diagnostics.md) |
| `event_manager` | Active runtime | D | [event_manager_init](../../components/event_manager/src/event_manager.c) creates the private loop; main also calls [event_registry_init](../../components/event_manager/src/event_registry.c). Registry is within this component. | [006](AUDIT-006-dispatcher-device-lifecycle.md) |
| `fan_controller_component` | Active runtime through callbacks | A | [fan_controller_init](../../components/fan_controller_component/src/fan_controller.c) is called by main and registers temperature/coordinator callbacks; no dedicated fan task. | [001](AUDIT-001-baseline-output-safety.md), [007](AUDIT-007-operations-diagnostics.md) |
| `gpio_master_driver` | Active library/driver | A | [GPIO APIs](../../components/gpio_master_driver/src/gpio_master_driver.c) are used by [heater_controller](../../components/heater_controller_component/src/heater_controller.c); execute in caller contexts. | [001](AUDIT-001-baseline-output-safety.md) |
| `health_monitor` | Dormant link input; startup disabled | F | Required by [main CMake](../../main/CMakeLists.txt); the call to [init_health_monitor](../../components/health_monitor/src/health_monitor_core.c) in main is commented out. | [007](AUDIT-007-operations-diagnostics.md) |
| `heater_controller_component` | Active runtime | A | Main calls [init_heater_controller_component](../../components/heater_controller_component/src/heater_controller_core.c); [PWM task](../../components/heater_controller_component/src/heater_controller_task.c) and [command handler](../../components/heater_controller_component/src/heater_controller_events.c) own output work. | [001](AUDIT-001-baseline-output-safety.md) |
| `heating_program_validation` | Active library under enabled HMI | C | [Validation APIs](../../components/heating_program_validation/src/heating_program_validation.c) are called by HMI program/run handlers; [HMI CMake](../../components/nextion_hmi/CMakeLists.txt) requires the component. | [004](AUDIT-004-hmi-persistence.md), [005](AUDIT-005-pid-profile-control.md) |
| `logger_component` | Active runtime | F | Main calls [logger_init](../../components/logger_component/src/logger_core.c), which creates its queue/task and invokes CLI/storage initialization. | [007](AUDIT-007-operations-diagnostics.md) |
| `modbus_master` | Active library with initialized external driver/service | B | Main calls [modbus_master_init](../../components/modbus_master/src/modbus_master_core.c), which creates/starts ESP-Modbus; application wrapper has no separate application-created worker. | [003](AUDIT-003-modbus-device-ownership.md) |
| `nextion_hmi` | Optional enabled, active | E | `CONFIG_NEXTION_HMI_ENABLED=y`; [CMake](../../components/nextion_hmi/CMakeLists.txt) and main gate it. [nextion_hmi_init](../../components/nextion_hmi/src/nextion_hmi.c) starts model/storage/UART/coordinator/RX work. | [004](AUDIT-004-hmi-persistence.md) |
| `pid_component` | Active library during profiles | C | [pid_controller_compute](../../components/pid_component/src/pid_component.c) is called by coordinator control work; no independent task. | [005](AUDIT-005-pid-profile-control.md) |
| `run_indicator` | Optional disabled; unused compiled/link-input archive | A | [CMake](../../components/run_indicator/CMakeLists.txt) registers source unconditionally; [run_indicator.c](../../components/run_indicator/src/run_indicator.c) gates implementation. Current config disables it; main has no startup call. | [001](AUDIT-001-baseline-output-safety.md) |
| `spi_master_component` | Dormant link input | F | External application callers of [init_spi/spi_transfer/shutdown_spi](../../components/spi_master_component/src/spi_master_component.c) were found only in the excluded legacy monitor. | [007](AUDIT-007-operations-diagnostics.md) |
| `temp_sensor_device` | Active library/device implementation | B | [temp_sensor_create](../../components/temp_sensor_device/src/temp_sensor_device_core.c) is called during processor initialization; update/read operations span manager/processor contexts. | [003](AUDIT-003-modbus-device-ownership.md) |
| `temperature_monitor_component` | Excluded legacy | F | Root [CMake](../../CMakeLists.txt) explicitly excludes it; local [CMake](../../components/temperature_monitor_component/CMakeLists.txt) identifies legacy SPI/MAX31865. No current startup call or stored compile command. | [007](AUDIT-007-operations-diagnostics.md) |
| `temperature_processor_component` | Active runtime | B | Main calls [init_temp_processor(5)](../../components/temperature_processor_component/src/temperature_processor_core.c); [worker](../../components/temperature_processor_component/src/temperature_processor_task.c) and [device-update event bridge](../../components/temperature_processor_component/src/temperature_processor_events.c) process owned sensors. | [002](AUDIT-002-temperature-authorization.md), [003](AUDIT-003-modbus-device-ownership.md) |
| `temperature_profile_controller` | Active library during profiles | C | [load_heating_profile/profile_tick](../../components/temperature_profile_controller/src/temperature_profile_core.c) are called by coordinator control; no independent task. | [005](AUDIT-005-pid-profile-control.md) |
| `time_component` | Configuration-only/build support | F | Only [CMake](../../components/time_component/CMakeLists.txt) and [Kconfig](../../components/time_component/Kconfig) exist; source glob is empty, no API or task implementation. | [007](AUDIT-007-operations-diagnostics.md), [008](AUDIT-008-configuration-build.md) |
| `transmitter_diagnostics` | Dormant link input | F | Required by main CMake; main's [transmitter_diagnostics_auto_init](../../components/transmitter_diagnostics/src/transmitter_diagnostics_auto.c) call is commented out; no active external diagnostic caller found. | [007](AUDIT-007-operations-diagnostics.md) |

Counts: 10 active-runtime entries, 6 active libraries/drivers, 1 optional enabled,
1 optional disabled, 4 dormant link inputs, 2 support-only entries, and 1 excluded
legacy entry. Primary packages: A 4, B 4, C 5, D 2, E 1, F 9 (total 25).

The stored compilation database describes 55 C translation units across 22
local entries. `common`/`time_component` have no C units; the legacy monitor is
excluded. The stored map extracts objects from 17 local entries including main;
the error manager, health monitor, diagnostics, indicator and SPI archives are
link inputs with no extracted members. These are stored-artifact observations.

## Cross-component review ownership

| Boundary | Primary reviewers | Integration requirement |
| --- | --- | --- |
| Sample cache → aggregate → expiry → output inhibit | B, A; D for concurrency | Match value/metadata ownership and expiry behavior to actual output requests. |
| Program/PID → dispatcher → heater | C, A; D for ordering | Match program transitions, pending commands, inhibits and worker completion. |
| HMI → program/settings/service records | E, C, F | Identify process ownership and external command requirements, including local HMI state dependencies. |
| Fault/health/logging → mitigation/status | A, F; D for backpressure | Distinguish fault reporting, diagnosis and direct mitigation; do not assume a subscriber exists. |
| Build/config → every runtime boundary | F plus affected package | Record effective units, values, feature gates and provenance; field configuration remains unverified. |

## Traceability status

This inventory is the coverage denominator for the full analysis. The lead will
link reviewed behavior/invariant IDs, reconciled finding IDs, and proposed or
executed acceptance scenarios as each package finishes. No behavior coverage or
regression coverage percentage is claimed from component enumeration.

The first reviewed slice is [current controller behavior](CURRENT-CONTROLLER-BEHAVIOR.md).
It is explicitly partial, not a final system specification:

| Primary package / components | Reviewed behavior IDs | Finding/candidate links | Proposed scenario IDs |
| --- | --- | --- | --- |
| A / `heater_controller_component`, `gpio_master_driver`, `fan_controller_component`, `run_indicator` | BHV-OUT-001–004; BHV-STATE-001–007 | F-070, F-074, F-075, F-077, F-078; `A-CAND-AUX-005` fan path | SCN-W1-001, 003, 005; SCN-W3-001, 002 |
| B / `device_manager`, `modbus_master`, `temp_sensor_device`, `temperature_processor_component` | BHV-TEMP-001–005; BHV-STATE-004–005 | F-071, F-060, F-063; `B-CAND-ZERO`, `B-CAND-COUNT` | SCN-W1-002, 004 |
| C / `common`, `coordinator_component`, `heating_program_validation`, `pid_component`, `temperature_profile_controller` | BHV-STATE-002–007; profile/control contract | F-069/F-070/F-076/F-077/F-078/F-061; `C-EASE-EST`, `C-MANUAL-RATE`, `C-DT-EXP` | SCN-W1-001, 002, 006; SCN-W3-001, 002 |
| D / `commands_dispatcher`, `event_manager`; cross-task lens across active/optional entries | BHV-STATE-003–007; concurrency/lifetime table in [Audit 006](AUDIT-006-dispatcher-device-lifecycle.md) | F-061/F-062/F-063/F-072/F-074/F-075; `D-CAND-SENSOR-SPLIT`, `D-CAND-TASK-HANDLE`, `D-CAND-HANDLER-LIFE`, `D-CAND-QUERY-SNAPSHOT` | SCN-D-001; callback/handler barrier oracles proposed |
| E / `nextion_hmi` | BHV-HMI-001–007 | F-073; F-014 residuals; `E-CAND-STATE-001`, `E-CAND-SERVICE-001`, `E-CAND-RESET-001` | SCN-E-001–007; SCN-F-001 |
| F / `main`, `debug_console`, `error_manager`, `health_monitor`, `logger_component`, `spi_master_component`, `temperature_monitor_component`, `time_component`, `transmitter_diagnostics` | BHV-OPS-001–006 | F-068, F-073; `F-CAND-LOG-001–004`, `F-CAND-SPI-001`; existing F-064–F-067 dormant paths | SCN-F-001; logging/storage fault injection proposed |

These links are proposed tests, not executed coverage. D reconciled
gate/write ordering and queue liveness as F-074/F-075; remaining conditional
callbacks/handler lifetimes need focused tests. F has configured/static budget evidence, not runtime resource
headroom; E's protocol requirements remain undecided.

| Work | Current evidence |
| --- | --- |
| Baseline/component inventory | Completed by read-only source/configuration/metadata checks. |
| A–C, E and F detailed reviews | Completed at source baseline; current behavior and unresolved candidates linked above. |
| D detailed review | Completed at source baseline with conditional candidate tests/upstream guarantees open. |
| Current-behavior specification | A–F/cross-component integrated draft linked; product-policy/field questions explicitly open. |
| Deterministic firmware tests | No harness or tests executed in this baseline step. |
| Field/bench validation | No hardware operated; deployment and protection facts remain open. |

# 05 — Configuration catalog

## 1. Three configurations exist, and none of them is "the" configuration

| Name | File | Tracked in git | What it is |
| --- | --- | --- | --- |
| Kconfig defaults | `components/*/Kconfig` `default` lines | yes | Values a brand-new configuration gets for a symbol absent from `sdkconfig.defaults` |
| Tracked baseline | `sdkconfig.defaults` (added in `196d2ff`) | yes | A copy of the hardening reviewer's resolved `sdkconfig` (ESP-IDF 5.5.4 environment). Described in F-049 as the "reviewed resolved configuration" and in the repository map as the authoritative deployment configuration |
| Field build | `sdkconfig` in the working copy (2026-06-05) | **no** (`.gitignore`) | The configuration the field-tuned 15 kW unit was built with (ESP-IDF 5.5.1 init). It predates three coordinator sensor symbols, so they are absent |

**Mistake left in place (F-067):** the tracked baseline is not the field configuration. A clean build from git today produces firmware that differs from what was validated in the field:

| Area | Field build | Tracked baseline | Consequence of building from git |
| --- | --- | --- | --- |
| Flash size | 8 MB | 2 MB | Wrong flash geometry for the 8 MB part |
| Partition table | `partitions.csv` (nvs 128 KB, factory 2 MB, **LittleFS `storage` 5.8 MB**) | `partitions_singleapp.csv` (no `storage`) | `logger_init_storage` cannot mount `storage`, so crash-log storage and the restart snapshot handler are never registered. The NVS partition changes size, which can trigger the boot-time NVS erase path and lose operating hours (F-073) |
| PID | Kp 100, Kd 30, out max 95 %, FF **on** (80 °C → 8 %, 150 °C → 12 %) | Kp 115, Kd 0, out max 100 %, FF off | Loses every tuning result from field rounds 1–4 |
| Overshoot | 20 °C | 30 °C | Slower runaway detection |
| Stall window | 300 s | 180 s | More nuisance stalls (reverts guard step 1) |
| Fan | 43 / 41 °C | 35 / 32 °C | Different idle fan behavior |
| Heater kW (display only) | 25 | 20 | Power readout differs |
| Max programs | 15 | 200 | |
| Sensor delta warning | 5 °C | 2 °C | Different warning noise |
| Logger | 265-byte messages, queue 128 | 64-byte messages, queue 64 | Truncated logs; RAM and RTC sizing differ |
| Run indicator | **enabled, GPIO 2** | disabled | The component is excluded from the production build now, so this setting is inert |

The tracked baseline matches the Kconfig defaults except for `COORDINATOR_OVERSHOOT_THRESHOLD_C`, `COORDINATOR_STALL_CHECK_MS`, `FAN_CONTROLLER_AUTO_ON/OFF_C` and `PID_KD`.

Repository docs written before this audit (REPOSITORY_MAP "Build" section, F-049, CHANGE-VALIDATION-F049) record the tracked values (overshoot 30, stall 180 s, Kd 0, fan 35/32, FF off) as "the reviewed configuration". The field-validated values are the ones in the right-hand column of the full table below.

Hardware note (D-04, 2026-10-13): the team does not know the old board's real flash size; the field `sdkconfig` states 8 MB, which is unverified. The new controller board has **16 MB**. The partition plan is deferred topic T-12. PID values must be preserved (D-05); how they are stored is T-03.

### Rule for the rewrite

Keep one version-controlled configuration per board/furnace variant (devicetree overlay plus `prj.conf`/board conf). Build it in CI, and print its identity (hash) at boot and in every crash record. Each tuning value lives in exactly one place. Never keep an untracked local config that differs from git.

## 2. Precision-bearing configuration (input for the rewrite's precision changes)

| Quantity | Current resolution / value | Where |
| --- | --- | --- |
| Program target temperature | 1 °C (`int`) | `program_stage_t.target_t_c` |
| Ramp rate | 0.1 °C/min (`x10`) | `delta_t_per_min_x10` |
| Stage time | 1 min | `t_min` |
| Phase/cooling tolerance | 2 °C (whole degrees) | `NEXTION_TEMP_TOLERANCE_C` |
| Heating→hold handover tolerance | 0.1 °C steps, 0.5 °C set | `COORDINATOR_HANDOVER_TOLERANCE_X10` |
| Guard thresholds | 1 °C steps | `COORDINATOR_*_C` |
| PID gains | 0.001 steps (thousandths) | `PID_KP/KI/KD` |
| PID output limits / FF points | 1 % steps | `PID_OUTPUT_*`, `PID_FF_PCT_*` |
| Sensor value | float32 from MS9024 (instrument resolution unknown) | reg 728 |
| Aggregate | mean of fresh sensors, float | processor |
| Displayed temperature | 0.01 °C formatting | HMI |
| Time base | 10 ms tick; elapsed ms in `uint32_t` (wraps after 49.7 days) | FreeRTOS |
| PID period | 1000 ms | `COORDINATOR_PID_TICK_INTERVAL_MS` |
| Actuator period / resolution | 5000 ms window, 10 ms tick, so 0.2 % duty steps; updated once per window | `HEATER_WINDOW_SIZE_MS` |
| Sensor poll period | ~1000 ms | `DEVICE_MANAGER_UPDATE_INTERVAL_MS` |

## 3. Full project symbol table

Generated on 2026-10-13 from the `components/*/Kconfig` files (legacy `temperature_monitor_component` and `spi_master_component` excluded), `sdkconfig.defaults` and the local `sdkconfig`. `(diff)` and bold mark symbols whose three values are not all equal. `absent` means the symbol does not appear in that file.

### `commands_dispatcher`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `COMMANDS_DISPATCHER_COMPONENT_ID` | Commands Dispatcher Component ID | 7 | 7 | 7 |
| `COMMANDS_DISPATCHER_HEARTBEAT_TIMEOUT_MS` | Commands Dispatcher Heartbeat Timeout (ms) | 10000 | 10000 | 10000 |
| `COMMANDS_DISPATCHER_MAX_HANDLERS` | Max number of command handlers | 5 | 5 | 5 |
| `COMMANDS_DISPATCHER_QUEUE_SIZE` | Commands Dispatcher Queue Size | 10 | 10 | 10 |
| `COMMANDS_DISPATCHER_TASK_NAME` | Commands Dispatcher Task Name | COMMANDS_DISPATCHER_NAME | COMMANDS_DISPATCHER_NAME | COMMANDS_DISPATCHER_NAME |
| `COMMANDS_DISPATCHER_TASK_PRIORITY` | Commands Dispatcher Task Priority | 5 | 5 | 5 |
| `COMMANDS_DISPATCHER_TASK_STACK_SIZE` | Commands Dispatcher Task Stack Size | 4096 | 4096 | 4096 |

### `coordinator_component`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `COORDINATOR_COMPONENT_ID` | Coordinator Component ID | 4 | 4 | 4 |
| `COORDINATOR_HANDOVER_TOLERANCE_X10` | Ramp->hold handover tolerance, tenths C (5 = 0.5 C) | 5 | 5 | 5 |
| `COORDINATOR_HEARTBEAT_TIMEOUT_MS` | Coordinator Heartbeat Timeout (ms) | 5000 | 5000 | 5000 |
| `COORDINATOR_HOLD_BAND_C` | Hold-phase deviation alarm band (C) | 4 | 4 | 4 |
| `COORDINATOR_HOLD_VIOLATION_MS` | Hold-phase deviation sustain time before pause (ms) | 300000 | 300000 | 300000 |
| `COORDINATOR_MAX_HEATING_PROFILE_STEPS` | Max heating profile steps | 20 | 20 | 20 |
| `COORDINATOR_MAX_PROFILES_STORED` | Max number of stored profiles | 5 | 5 | 5 |
| `COORDINATOR_OVERSHOOT_THRESHOLD_C` (diff) | Maximum allowed overshoot above setpoint (C) | **20** | **30** | **20** |
| `COORDINATOR_PID_TICK_INTERVAL_MS` | PID control loop tick interval (ms) | 1000 | 1000 | 1000 |
| `COORDINATOR_PROFILE_COMPLETE_TEMP_C` | Temperature threshold for profile completion (C) | 40 | 40 | 40 |
| `COORDINATOR_RAMP_EASE_BAND_C` | Ramp soft-landing band before hold (C) | 6 | 6 | 6 |
| `COORDINATOR_SENSOR_AGGREGATE_STALE_TIMEOUT_MS` (diff) | Maximum sensor aggregate age (ms) | **5000** | **5000** | **absent** |
| `COORDINATOR_SENSOR_AUTO_RECOVERY` (diff) | Automatically recover from sensor-data inhibit | **y** | **y** | **absent** |
| `COORDINATOR_SENSOR_RECOVERY_VALID_AGGREGATES` (diff) | Consecutive valid aggregates before sensor recovery | **3** | **3** | **absent** |
| `COORDINATOR_STALL_CHECK_MS` (diff) | Heating stall detection window (ms) | **300000** | **180000** | **300000** |
| `COORDINATOR_STALL_MIN_LAG_C` | Heating stall minimum lag below setpoint (C) | 5 | 5 | 5 |
| `COORDINATOR_STALL_RATE_FRACTION_PCT` | Heating stall rate fraction (% of programmed ramp) | 50 | 50 | 50 |
| `COORDINATOR_TASK_NAME` | Coordinator Task Name | COORDINATOR_TASK | COORDINATOR_TASK | COORDINATOR_TASK |
| `COORDINATOR_TASK_PRIORITY` | Coordinator Task Priority | 5 | 5 | 5 |
| `COORDINATOR_TASK_STACK_SIZE` | Coordinator Task Stack Size | 4096 | 4096 | 4096 |

### `debug_console`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `DEBUG_CONSOLE_MAX_ARGUMENTS` | Debug Console Max Arguments | 8 | 8 | 8 |
| `DEBUG_CONSOLE_MAX_COMMAND_LENGTH` | Debug Console Max Command Length | 256 | 256 | 256 |
| `DEBUG_CONSOLE_MAX_REGISTERED_COMMANDS` | Debug Console Max Registered Commands | 20 | 20 | 20 |
| `DEBUG_CONSOLE_TASK_NAME` | Debug Console Task Name | debug_console_task | debug_console_task | debug_console_task |
| `DEBUG_CONSOLE_TASK_PRIORITY` | Debug Console Task Priority | 2 | 2 | 2 |
| `DEBUG_CONSOLE_TASK_STACK_SIZE` | Debug Console Task Stack Size | 4096 | 4096 | 4096 |

### `device_manager`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `DEVICE_MANAGER_COMPONENT_ID` | Device Manager Component ID | 1 | 1 | 1 |
| `DEVICE_MANAGER_DE_PIN` | Modbus DE Pin | 25 | 25 | 25 |
| `DEVICE_MANAGER_HEARTBEAT_TIMEOUT_MS` | Device Manager Heartbeat Interval (ms) | 5000 | 5000 | 5000 |
| `DEVICE_MANAGER_MAX_DEVICES` | Maximum number of devices | 32 | 32 | 32 |
| `DEVICE_MANAGER_MODBUS_BAUD_RATE` | Modbus Baud Rate | 9600 | 9600 | 9600 |
| `DEVICE_MANAGER_MODBUS_RX_PIN` | Modbus RX Pin | 26 | 26 | 26 |
| `DEVICE_MANAGER_MODBUS_TX_PIN` | Modbus TX Pin | 27 | 27 | 27 |
| `DEVICE_MANAGER_MODBUS_UART_NUMBER` | Modbus UART Number | 2 | 2 | 2 |
| `DEVICE_MANAGER_TASK_NAME` | Device Manager Task Name | device_manager_task | device_manager_task | device_manager_task |
| `DEVICE_MANAGER_TASK_PRIORITY` | Device Manager Task Priority | 5 | 5 | 5 |
| `DEVICE_MANAGER_TASK_STACK_SIZE` | Device Manager Task Stack Size | 4096 | 4096 | 4096 |
| `DEVICE_MANAGER_UPDATE_INTERVAL_MS` | Device Manager Update Interval (ms) | 1000 | 1000 | 1000 |

### `error_manager`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `ERROR_MANGER_MAX_MODULES` | Max number of modules tracked | 10 | 10 | 10 |

### `event_manager`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `EVENT_MANAGER_QUEUE_SIZE` | Event queue size | 32 | 32 | 32 |
| `EVENT_MANAGER_TASK_NAME` | Event manager task name | event_manager_task | event_manager_task | event_manager_task |
| `EVENT_MANAGER_TASK_PRIORITY` | Event manager task priority | 5 | 5 | 5 |
| `EVENT_MANAGER_TASK_STACK_SIZE` | Event manager task stack size | 4096 | 4096 | 4096 |

### `fan_controller_component`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `FAN_CONTROLLER_AUTO_OFF_C` (diff) | Auto-off threshold (C) | **40** | **32** | **41** |
| `FAN_CONTROLLER_AUTO_ON_C` (diff) | Auto-on threshold (C) | **43** | **35** | **43** |
| `FAN_CONTROLLER_GPIO_PIN` | Fan Controller GPIO Pin | 19 | 19 | 19 |

### `health_monitor`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `HEALTH_MONITOR_CHECK_INTERVAL_MS` | Health monitor check interval in milliseconds | 2000 | 2000 | 2000 |
| `HEALTH_MONITOR_COORDINATOR_MAX_MISSES` | Health monitor coordinator max misses count | 2 | 2 | 2 |
| `HEALTH_MONITOR_COORDINATOR_MAX_SILENCE_MS` | Health monitor coordinator max silence interval in ms | 2000 | 2000 | 2000 |
| `HEALTH_MONITOR_HEATER_CONTROLLER_MAX_MISSES` | Health monitor heater controller max misses count | 2 | 2 | 2 |
| `HEALTH_MONITOR_HEATER_CONTROLLER_MAX_SILENCE_MS` | Health monitor heater controller max silence interval in ms | 2000 | 2000 | 2000 |
| `HEALTH_MONITOR_TASK_NAME` | Health monitor task name | health_monitor_task | health_monitor_task | health_monitor_task |
| `HEALTH_MONITOR_TASK_PRIORITY` | Health monitor task priority | 6 | 6 | 6 |
| `HEALTH_MONITOR_TASK_STACK_SIZE` | Health monitor task stack size | 4096 | 4096 | 4096 |
| `HEALTH_MONITOR_TEMP_MONITOR_MAX_MISSES` | Health monitor temp monitor max misses count | 2 | 2 | 2 |
| `HEALTH_MONITOR_TEMP_MONITOR_MAX_SILENCE_MS` | Health monitor temp monitor max silence interval in ms | 2000 | 2000 | 2000 |
| `HEALTH_MONITOR_TEMP_PROCESSOR_MAX_MISSES` | Health monitor temp processor max misses count | 2 | 2 | 2 |
| `HEALTH_MONITOR_TEMP_PROCESSOR_MAX_SILENCE_MS` | Health monitor temp processor max silence interval in ms | 2000 | 2000 | 2000 |
| `HEARTH_BEAT_COUNT` | Number of components to monitor for heartbeat | 8 | 8 | 8 |

### `heater_controller_component`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `HEATER_CONTACTOR_GPIO_PIN` | GPIO Pin | 22 | 22 | 22 |
| `HEATER_CONTROLLER_COMPONENT_ID` | Heater Controller Component ID | 2 | 2 | 2 |
| `HEATER_CONTROLLER_HEARTBEAT_TIMEOUT_MS` | Heater Controller Heartbeat Timeout (ms) | 15000 | 15000 | 15000 |
| `HEATER_CONTROLLER_TASK_NAME` | Heater Controller Task Name | heater_controller_task | heater_controller_task | heater_controller_task |
| `HEATER_CONTROLLER_TASK_PRIORITY` | Heater Controller Task Priority | 5 | 5 | 5 |
| `HEATER_CONTROLLER_TASK_STACK_SIZE` | Heater Controller Task Stack Size | 4096 | 4096 | 4096 |
| `HEATER_SSR_GPIO_PIN` | GPIO Pin | 21 | 21 | 21 |
| `HEATER_WINDOW_SIZE_MS` | Heater Window Size (ms) | 5000 | 5000 | 5000 |

### `logger_component`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `LOG_ENABLE` | Enable logging | y | y | y |
| `LOG_LEVEL` | Log level (0=ERROR ... 4=VERBOSE) | 4 | 4 | 4 |
| `LOG_MAX_MESSAGE_LENGTH` (diff) | Max message length | **64** | **64** | **265** |
| `LOG_MAX_RING_BUFFER_SIZE` | Max ring buffer size | 100 | 100 | 100 |
| `LOG_PANIC_BUFFER_COUNT` | Max panic buffer size | 20 | 20 | 20 |
| `LOG_QUEUE_SIZE` (diff) | Queue size | **64** | **64** | **128** |
| `LOG_TASK_NAME` | Logging task name | LOG_TASK | LOG_TASK | LOG_TASK |
| `LOG_TASK_PRIORITY` | Logging task priority | 4 | 4 | 4 |
| `LOG_TASK_STACK_SIZE` | Logging task stack size | 4096 | 4096 | 4096 |

### `nextion_hmi`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `NEXTION_COOLDOWN_RATE_X10` | Cooldown rate x10 (10 = 1.0 C/min) | 10 | 10 | 10 |
| `NEXTION_COORDINATOR_COMPONENT_ID` | HMI Coordinator Component ID | 5 | 5 | 5 |
| `NEXTION_COORDINATOR_HEARTBEAT_TIMEOUT_MS` | HMI Coordinator Heartbeat Timeout (ms) | 5000 | 5000 | 5000 |
| `NEXTION_COORDINATOR_QUEUE_DEPTH` | Command queue depth | 8 | 8 | 8 |
| `NEXTION_COORDINATOR_TASK_PRIORITY` | Coordinator task priority | 5 | 5 | 5 |
| `NEXTION_COORDINATOR_TASK_STACK_SIZE` | Coordinator task stack size (bytes) | 8192 | 8192 | 8192 |
| `NEXTION_DELTA_TEMP_TOLERANCE_C_X10` | Delta-temp tolerance x10 (5 = 0.5 C/min) | 5 | 5 | 5 |
| `NEXTION_DELTA_T_MAX_PER_MIN_X10` | Max heating rate x10 (30 = 3.0 C/min) | 30 | 30 | 30 |
| `NEXTION_DELTA_T_MIN_PER_MIN_X10` | Max cooling rate x10 (-30 = -3.0 C/min) | -30 | -30 | -30 |
| `NEXTION_FILE_READ_CHUNK_SIZE` | File read chunk size (bytes) | 512 | 512 | 512 |
| `NEXTION_FILE_WRITE_CHUNK_SIZE` | File write chunk size (bytes) | 512 | 512 | 512 |
| `NEXTION_GRAPH_DISP_ID` | Main graph Nextion component ID | 6 | 6 | 6 |
| `NEXTION_HAS_BLUETOOTH` | Device has Bluetooth capability | n | n | n |
| `NEXTION_HAS_WIFI` | Device has WiFi capability | n | n | n |
| `NEXTION_HEATER_POWER_KW` (diff) | Heater power (kW) | **20** | **20** | **25** |
| `NEXTION_HMI_ENABLED` | Enable Nextion HMI | y | y | y |
| `NEXTION_HMI_PROFILE_SLOT_COUNT` | Heating profile slot count | 1 | 1 | 1 |
| `NEXTION_LINE_BUF_SIZE` | Line buffer size for RX parsing (bytes) | 256 | 256 | 256 |
| `NEXTION_MAIN_GRAPH_HEIGHT` | Main page graph height (px) | 321 | 321 | 321 |
| `NEXTION_MAIN_GRAPH_WIDTH` | Main page graph width (px) | 764 | 764 | 764 |
| `NEXTION_MAX_OPERATIONAL_TIME_MIN` | Maximum total program time (minutes) | 10080 | 10080 | 10080 |
| `NEXTION_MAX_PROGRAMS` (diff) | Maximum number of stored programs | **200** | **200** | **15** |
| `NEXTION_MAX_TEMPERATURE_C` | Maximum temperature (C) | 170 | 170 | 170 |
| `NEXTION_MIN_OPERATIONAL_TIME_MIN` | Minimum total program time (minutes) | 1 | 1 | 1 |
| `NEXTION_MIN_TEMPERATURE_C` | Minimum temperature (C) | 20 | 20 | 20 |
| `NEXTION_OP_TIME_OVERRIDE_ENABLE` | Override stored operational time on boot (service tool) | n | n | n |
| `NEXTION_OP_TIME_OVERRIDE_HOURS` (diff) | Operational time override value (hours) | **0** | **absent** | **absent** |
| `NEXTION_PAGE_MAIN` | Main page name | main_page | main_page | main_page |
| `NEXTION_PAGE_PROGRAMS` | Programs page name | programs_page | programs_page | programs_page |
| `NEXTION_PAGE_SETTINGS` | Settings page name | settings_page | settings_page | settings_page |
| `NEXTION_PROGRAMS_GRAPH_HEIGHT` (diff) | Programs page graph height (px) | **449** | **449** | **450** |
| `NEXTION_PROGRAMS_GRAPH_ID` | Programs graph Nextion component ID | 50 | 50 | 50 |
| `NEXTION_PROGRAMS_GRAPH_WIDTH` | Programs page graph width (px) | 1024 | 1024 | 1024 |
| `NEXTION_PROGRAMS_PAGE_COUNT` | Number of editor pages | 3 | 3 | 3 |
| `NEXTION_PROGRAMS_PAGE_STAGE_COUNT` | Stages displayed per page | 5 | 5 | 5 |
| `NEXTION_PROGRAM_FILE_EXTENSION` | Program file extension | .prg | .prg | .prg |
| `NEXTION_PROGRAM_FILE_SIZE` | Program file buffer size (bytes) | 4096 | 4096 | 4096 |
| `NEXTION_RX_COMPONENT_ID` | Nextion RX Component ID | 6 | 6 | 6 |
| `NEXTION_RX_HEARTBEAT_TIMEOUT_MS` | Nextion RX Heartbeat Timeout (ms) | 5000 | 5000 | 5000 |
| `NEXTION_RX_TASK_PRIORITY` | RX task priority | 10 | 10 | 10 |
| `NEXTION_RX_TASK_STACK_SIZE` | RX task stack size (bytes) | 4096 | 4096 | 4096 |
| `NEXTION_SENSOR_READ_FREQUENCY_SEC` | Sensor read frequency (seconds) | 1 | 1 | 1 |
| `NEXTION_TEMP_TOLERANCE_C` | Temperature tolerance (C) | 2 | 2 | 2 |
| `NEXTION_TIME_TOLERANCE_SEC` | Time tolerance (seconds) | 30 | 30 | 30 |
| `NEXTION_T_DELTA_MIN_MIN` | Minimum time delta per stage (minutes) | 1 | 1 | 1 |
| `NEXTION_UART_BAUD_RATE` | UART baud rate | 115200 | 115200 | 115200 |
| `NEXTION_UART_PACKET_ACK_TIMEOUT_MS` | UART packet ACK timeout (ms) | 500 | 500 | 500 |
| `NEXTION_UART_PORT_NUM` | UART port number (0, 1 or 2) | 1 | 1 | 1 |
| `NEXTION_UART_RESPONSE_TIMEOUT_MS` | UART response timeout (ms) | 2000 | 2000 | 2000 |
| `NEXTION_UART_RX_BUF_SIZE` | UART RX buffer size (bytes) | 2048 | 2048 | 2048 |
| `NEXTION_UART_RX_PIN` | UART RX GPIO pin | 33 | 33 | 33 |
| `NEXTION_UART_TX_BUF_SIZE` | UART TX buffer size (bytes) | 2048 | 2048 | 2048 |
| `NEXTION_UART_TX_PIN` | UART TX GPIO pin | 32 | 32 | 32 |

### `pid_component`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `PID_ADAPTIVE_BAND_CLOSE_C` (diff) | Adaptive: close-band threshold (deg C) | **2** | **absent** | **absent** |
| `PID_ADAPTIVE_BAND_FAR_C` (diff) | Adaptive: full-power band threshold (deg C) | **20** | **absent** | **absent** |
| `PID_ADAPTIVE_BAND_MID_C` (diff) | Adaptive: mid-band threshold (deg C) | **10** | **absent** | **absent** |
| `PID_ADAPTIVE_BAND_NEAR_C` (diff) | Adaptive: near-band threshold (deg C) | **5** | **absent** | **absent** |
| `PID_ADAPTIVE_FACTOR_CLOSE_PCT` (diff) | Adaptive: close-band output factor (% of OUTPUT_MAX) | **25** | **absent** | **absent** |
| `PID_ADAPTIVE_FACTOR_HOLD_PCT` (diff) | Adaptive: hold-band output factor (% of OUTPUT_MAX) | **15** | **absent** | **absent** |
| `PID_ADAPTIVE_FACTOR_MID_PCT` (diff) | Adaptive: mid-band output factor (% of OUTPUT_MAX) | **70** | **absent** | **absent** |
| `PID_ADAPTIVE_FACTOR_NEAR_PCT` (diff) | Adaptive: near-band output factor (% of OUTPUT_MAX) | **40** | **absent** | **absent** |
| `PID_ADAPTIVE_OUTPUT_LIMIT_ENABLED` | Enable adaptive output limit near setpoint | n | n | n |
| `PID_FEEDFORWARD_ENABLED` (diff) | Enable feedforward (add steady-state power model to output) | **n** | **n** | **y** |
| `PID_FF_PCT_HIGH` (diff) | Feedforward: hold duty at high temp (% of full SSR duty) | **0** | **0** | **12** |
| `PID_FF_PCT_LOW` (diff) | Feedforward: hold duty at low temp (% of full SSR duty) | **0** | **0** | **8** |
| `PID_FF_T_HIGH_C` (diff) | Feedforward: high calibration temperature (deg C) | **200** | **200** | **150** |
| `PID_FF_T_LOW_C` (diff) | Feedforward: low calibration temperature (deg C) | **100** | **100** | **80** |
| `PID_KD` (diff) | Kd - Derivative gain (thousandths; 50 = 0.050) | **30** | **0** | **30** |
| `PID_KI` | Ki - Integral gain (thousandths; 20 = 0.020) | 1 | 1 | 1 |
| `PID_KP` (diff) | Kp - Proportional gain (thousandths; 300 = 0.300) | **115** | **115** | **100** |
| `PID_OUTPUT_MAX` (diff) | Output maximum (% duty) | **100** | **100** | **95** |
| `PID_OUTPUT_MIN` | Output minimum (% duty) | 0 | 0 | 0 |

### `run_indicator`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `RUN_INDICATOR_ENABLED` (diff) | Enable run indicator | **n** | **n** | **y** |
| `RUN_INDICATOR_GPIO` (diff) | Run Indicator GPIO Pin | **-1** | **absent** | **2** |

### `temp_sensor_device`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `TEMP_SENSOR_DEVICE_MAX_DEVICES` | Maximum number of temperature sensor devices | 16 | 16 | 16 |
| `TEMP_SENSOR_MODBUS_START_ADDRESS` | Modbus start address for temperature sensor devices | 1 | 1 | 1 |

### `temperature_processor_component`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `TEMP_DELTA_THRESHOLD` (diff) | Temperature delta warning threshold (°C) | **2** | **2** | **5** |
| `TEMP_PROCESSOR_COMPONENT_ID` | Temperature processor component ID | 3 | 3 | 3 |
| `TEMP_PROCESSOR_HEART_BEAT_TIMEOUT_MS` | Temperature processor heartbeat interval in milliseconds | 5000 | 5000 | 5000 |
| `TEMP_PROCESSOR_TASK_NAME` | Temperature processor task name | TEMP_PROCESSOR_TASK | TEMP_PROCESSOR_TASK | TEMP_PROCESSOR_TASK |
| `TEMP_PROCESSOR_TASK_PRIORITY` | Temperature processor task priority | 5 | 5 | 5 |
| `TEMP_PROCESSOR_TASK_STACK_SIZE` | Temperature processor task stack size | 4096 | 4096 | 4096 |
| `TEMP_PROCESSOR_TEMPERATURE_BUFFER_SIZE` | Temperature processor buffer size | 10 | 10 | 10 |
| `TEMP_SENSORS_MAX_SENSORS` | Maximum number of active temperature sensors | 9 | 9 | 9 |

### `time_component`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `TIME_COMPONENT_TASK_NAME` | Time component processor task name | TIME_TASK | TIME_TASK | TIME_TASK |
| `TIME_COMPONENT_TASK_PRIORITY` | Time component processor task priority | 5 | 5 | 5 |
| `TIME_COMPONENT_TASK_STACK_SIZE` | Time component processor task stack size | 4096 | 4096 | 4096 |

### `transmitter_diagnostics`

| Symbol | Meaning | Kconfig default | Tracked `sdkconfig.defaults` | Field `sdkconfig` |
| --- | --- | --- | --- | --- |
| `TRANSMITTER_DIAGNOSTICS_SCAN_END_ADDR` | Modbus slave scan range end | 16 | 16 | 16 |
| `TRANSMITTER_DIAGNOSTICS_SCAN_START_ADDR` | Modbus slave scan range start | 1 | 1 | 1 |

## 4. Non-project ESP-IDF settings that matter to safety

| Setting | Tracked value | Note |
| --- | --- | --- |
| `ESP_TASK_WDT_EN` / `INIT` / timeout | y / y / 5 s, idle tasks of both cores only | No application task subscribes; a wedged application task does not reset the chip (F-036) |
| `ESP_INT_WDT` | y, 300 ms | |
| `ESP_BROWNOUT_DET` | y, level 0 | Output state during brownout unverified (F-047) |
| `ESP_SYSTEM_PANIC_PRINT_REBOOT` | y, 0 s delay | Panic reboots; RTC log snapshot is **not** captured on panic (F-068) |
| `FREERTOS_HZ` | 100 | 10 ms tick limits SSR on-time resolution |
| `LOG_DEFAULT_LEVEL` (ESP) / `LOG_LEVEL` (project) | INFO / 4 (DEBUG) | High log volume (F-074) |

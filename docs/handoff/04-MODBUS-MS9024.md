# 04 — Modbus RTU transport and MS9024 register work

> **Status for the new project (D-10, 2026-10-13):** custom thermal sensors will likely replace the MS9024, and F-066 will not be fixed. Keep this document as history and for its lessons (§6). The sensor hardware and interface are deferred topic T-07; ask the user before designing them.

## 1. Bus and transport

| Item | Value | Evidence |
| --- | --- | --- |
| Physical layer | RS-485 half-duplex, ESP32 UART2, TX 27 / RX 26 / DE 25 (`uart_set_mode(UART_MODE_RS485_HALF_DUPLEX)`) | `modbus_master_core.c:modbus_master_init` |
| Framing | Modbus RTU, 9600 baud, 8 data bits, no parity, 1 stop bit | same |
| Stack | `espressif/esp-modbus ^2.1.2` master (`mbc_master_create_serial`, `mbc_master_send_request`) | `main/idf_component.yml` |
| Response timeout | 300 ms, hard-coded (reduced from 1000 ms during field tests) | `ser_opts.response_tout_ms` |
| Retries | None in the application; a failed read fails the device update for that cycle | `temp_sensor_update` |
| Function codes used | 0x03 read holding registers (all reads), 0x06 write single register (diagnostic and repair writes only) | `modbus_master_read_register(s)`, `modbus_master_write_register` |
| Serialization | Only the device-manager task issues requests at runtime; no application lock. The optional diagnostics task would race it if started | inferred |
| Addresses | Sensor *i* (pool index 0…) uses slave address `TEMP_SENSOR_MODBUS_START_ADDRESS + i` = 1…5 | `temp_sensor_device_core.c:temp_sensor_create` |
| Poll pattern | Each 1 s cycle reads every RUNNING sensor in order with 100 ms between devices; one event after the pass | `device_manager_task.c` |

Byte budget per PV read at 9600 baud: request 8 bytes + response 9 bytes ≈ 18 ms on the wire plus device turnaround. A timed-out sensor costs 300 ms; five failing sensors stretch the cycle to about 2 s, still inside the 5 s freshness budget.

## 2. MS9024 register map as used and learned

The MS9024 is a NOVUS-family single-channel RTD/thermocouple transmitter with Modbus. Addresses are the protocol (zero-based) register numbers the firmware sends.

| Reg | Name | Type | Firmware use | Healthy reference value (unit at addr 3) |
| --- | --- | --- | --- | --- |
| 26 | FIN, input filter | u16 (LSB) | Logged by `ms9024_log_config` | 50 (bad unit had 127) |
| 27 | unknown (suspected offset/CJC/linearisation) | u16 | Repair table | 0 (bad unit had 200) |
| 28 | SENS, sensor type | u16 (LSB) | Logged; warns if not Pt100 | 16 = Pt100 α385 |
| 30 | unknown config | u16 | Repair table | 2 (bad unit had 0) |
| 32 | WIRE, wiring mode | u16 (LSB) | Logged; warns if not 4-wire | 4 = 4-wire |
| 107–121 | calibration table (inferred) | u16 | Diagnostic scan only | ~0x549E–0x54C3 |
| 126 | firmware version | u16 | Bus-probe register for diagnostics | 101 |
| 127 | device ID (preset; equals slave address on the reference unit) | u16 | Read once at sensor init and stored as `device_id` | 3 |
| 129 | unknown config/calibration | u16 | Repair table writes **282 (wrong)** | **265** (bad unit had 0) |
| 447 | C/F select | u16/coil | Logged; warns if Fahrenheit | 0 = Celsius |
| 524 | IN_OFFSET | float (2 regs) | Logged | 0.0 |
| 526 / 528 | unknown floats | float | Diagnostic scan | 0.0594 / 20.0000 |
| 726 | AOUT, analog output | float | Logged | 4.3179 (mA) |
| **728** | **PV, process value** | **float (2 regs)** | **Polled every cycle; the only control input** | 19.8986 °C at capture |
| 730 | T2, cold junction / board | float | Logged | 0.0 |
| 214–220 | unknown | u16 | Raw debug dump in `ms9024_log_config` | — |

Observations recorded in `components/temp_sensor_device/ms9024_healthy_registers.txt`:

- Odd registers in the 510–540 float area return `INVALID_RESPONSE` when read singly. They must be read as two-register floats.
- Regs 458–459 failed to read during capture.
- Sensor-type codes known to the firmware (`ms9024.c:sensor_name`): 0–8 thermocouples J, K, S, B, T, E, N, R, C; 10–13 linear 4–20 mA / 0–20 mA / 0–1 V / 0–10 V; 14–19 Pt10/50/100/200/500/1000 α385; 20 Pt100 α392; 21 Pt100 α391; 22 Cu100; 23 Ni100; 24 Ni120.

Primary datasheet confirmation of the register widths, float word order and meaning of regs 27/30/129 is still missing (F-044).

## 3. Float decoding: CDAB

```
regs[0] = first register returned  (low word,  "CD")
regs[1] = second register returned (high word, "AB")
raw     = (regs[1] << 16) | regs[0]
value   = bit-cast(raw) as IEEE-754 float
```

Source `modbus_utils.c:modbus_master_swap_float_cdab`. Evidence for this word order is empirical only: it decodes plausible values (for example 19.90 °C ambient in the healthy-unit capture), and the field runs controlled to it. No reference-thermometer comparison and no datasheet citation is recorded. Acceptance rules in `ms9024_read_float`, in order:

1. Transport error: reject.
2. NaN or Inf: reject.
3. `raw == 0x80000000` (−0.0): reject. The MS9024 returns this before it has a measurement.
4. Value outside −200…1500 °C: reject.

Every decode is logged at INFO with the raw hex, about 5 lines per second (F-074).

## 4. Diagnostics and repair work (chronological)

1. **Bad-unit investigation (early 2026).** Per the code comments, one transmitter read with an offset. A full register scan (`ms9024_diagnostic_scan`: regs 0–50, 100–130, 440–460, 510–540, plus decoded floats) was diffed against a good unit. The differences were regs 26, 27, 30 and 129.
2. **Auto-repair at init (since disabled).** `temp_sensor_init` used to call `ms9024_repair_from_good_unit` whenever the first PV read failed, including transient bus warm-up timeouts. That wrote hard-coded values to regs 27, 30 and 129 on any slave. It was disabled because a timeout is not evidence of corruption and blind writes can mis-configure a healthy unit.
3. **Healthy baseline (field-tests).** A boot dump of the healthy unit at address 3 was saved as `ms9024_healthy_registers.txt`. It showed the hard-coded reg-129 value 282 was **wrong** (healthy = 265).
4. **Full-register verification (hardening, F-025).** `ms9024_write_and_verify` and `ms9024_auto_correct_register` now compare all 16 bits of the read-back (they used to compare only the low byte).
5. **Transmitter diagnostics component.** `transmitter_diagnostics` scans addresses 1–16 by reading reg 126, then dumps the same ranges. It is linked in `main` but `transmitter_diagnostics_auto_init()` is commented out.

### Mistakes still in place

- The repair table in `ms9024.c:ms9024_repair_from_good_unit` still writes **reg 129 = 282** and omits reg 26 (F-066). It has no in-tree caller; it is reachable only through `TEMP_SENSOR_REPAIR_FORM_GOOD_UNIT` via `temp_sensor_write_device`.
- Regs 107–129 look like per-unit calibration. Copying them between units can corrupt calibration. Treat any repair as a manual, per-unit, verified procedure.
- The repair log tells the user to disable `CONFIG_MODBUS_TEMP_REPAIR_BAD_UNIT`, which does not exist.
- `ms9024_auto_correct_register` logs "OK … matches config" even after it corrected a mismatch.
- The Modbus component's Kconfig menu is titled "Device Manager Configuration" and is empty; transport parameters live in the device-manager Kconfig.

## 5. Device abstraction (`device_manager`, `temp_sensor_device`)

- `device_manager` keeps a fixed table of 32 `device_t` slots (`state`, `type`, `ctx`, `ops{init, update, read, write, shutdown}`). The task calls `update` on RUNNING devices; `read` returns the cache.
- `temp_sensor_device` has a static pool of 16 contexts: `last_temperature`, `last_update_tick`, `has_successful_sample`, Modbus address, register, and `device_id` from reg 127.
- Creation order: reserve the pool slot, create the device-manager entry (which calls `init` and reads reg 127, failure tolerated), then the processor sets the state to RUNNING.
- The table, states and per-sensor cache are written and read across tasks without locks (F-071). There is no reconnect logic: a sensor that fails simply stays stale until a read succeeds again.

## 6. Requirements for the new Modbus layer

- One owner thread for the bus. Request and response are bounded by a timeout. Count and expose per-sensor error statistics.
- Carry the measurement, a monotonic timestamp, and the status together as one atomically published record.
- Make the word order, sanity range, sentinel pattern and register addresses configuration constants with unit tests (decode golden vectors, including `0x80000000`, NaN, Inf and out-of-range).
- Commissioning tools (scan, dump, compare against a baseline) must be read-only by default. Any register write requires an explicit per-unit command with read-back verification and a baseline file, never automatic.
- Open: confirm against the NOVUS MS9024 manual (register list, holding versus input registers, EEPROM write endurance, float order setting if configurable, meaning of regs 27/30/129).

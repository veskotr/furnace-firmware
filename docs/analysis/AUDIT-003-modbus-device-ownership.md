# Audit 003: Modbus transport and device ownership

- **Date:** 2026-08-04
- **Commit:** `1fcb4b1` (`audit-docs-verification`)
- **Scope:** Modbus request serialization, MS9024 cache lifecycle, device-manager ownership, and control-input validity boundary
- **Hardware state:** not operated

## Protocol and ownership map

`modbus_master` creates one ESP-Modbus RTU serial master on configured UART2/9600 with a 300 ms response timeout. The local wrapper does not add a mutex, but the bundled `espressif/esp-modbus` serial implementation takes `mbm_opts->mbm_sema` around each `mbc_serial_master_send_request`; current source therefore serializes concurrent wire requests. This does not validate RS-485 wiring, device register semantics, or timeout/reconnect behavior.

The device manager owns the table and invokes each running device’s update function. `temp_sensor_update` reads PV register 728, rejects NaN/Inf, transmitter no-reading `-0.0`, and values outside -200…1500 C, then stores the cached float/tick/success state. The temperature processor consumes that cache to form an aggregate. Its freshness policy is implemented above the transport and remains governed by F-001/F-054.

## Verified and open results

| Area | Source result | Evidence limit / required next check |
| --- | --- | --- |
| Modbus request concurrency | Bundled serial master serializes requests with `mbm_sema`. | Verify shutdown/request concurrency separately; bench-test RS-485 timeout/reconnect and register order. |
| Float conversion | CDAB conversion is used; NaN/Inf, `-0.0`, and range failures are rejected. | MS9024 word order/register specification remains external-device evidence. |
| Cache coherence | **F-060 confirmed:** manager writes and processor reads cache fields without synchronization. | Add coherent-snapshot regression before relying on aggregate integrity. |
| Device table lifecycle | `device_manager_destroy` mutates table entries without an explicit table lock. | F-056/F-058 destruction/reinit races need a dedicated interleaving review. |
| Subscription failure | Temperature processor ignores the return from its device-manager event subscription. | Include in initialization fault-injection matrix. |

## Next verification slice

Map HMI command/persistence producers and configuration gates, then return to device-table destruction with a focused FreeRTOS lifecycle review. No hardware action is authorized.

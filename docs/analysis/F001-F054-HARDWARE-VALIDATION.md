# F-001/F-054 powered-controller validation plan

- **Status:** planned; not authorized or executed
- **Firmware identity:** record commit, working-tree diff or commit hash, resolved `sdkconfig` hash, binary SHA-256, ESP-IDF version, and build command.
- **Level:** powered-controller bench with no furnace thermal load; powered-furnace validation is out of scope.

## Claim under test

After the last accepted aggregate exceeds `CONFIG_COORDINATOR_SENSOR_AGGREGATE_STALE_TIMEOUT_MS`, the coordinator expiry timer directly requests sensor-data output inhibition, which must drive SSR control and contactor control to their schematic-defined inactive states without waiting for the dispatcher, HMI, or another temperature event.

## Prerequisites and controls

- Explicit authorization, qualified supervision, schematic-confirmed SSR/contactor polarity, and an independent emergency disconnect.
- Isolated controller bench with no furnace load; preserve all existing electrical interlocks.
- Logic analyzer or isolated measurement equipment on SSR-control, contactor-control, and a timestamped temperature/Modbus activity signal.
- Verified safe initial state: contactor de-energized, SSR control inactive, demand zero, and no active profile.
- A reversible, approved way to stop temperature updates after at least three valid aggregates without changing wiring or bypassing interlocks.
- Record the configured stale timeout and PID tick interval; the expiry deadline must be assessed against the stale timeout, not merely the next PID tick.

## Procedure

1. Record firmware/config identity and verify inactive outputs at boot.
2. Establish the required valid aggregates and verify the recoverable sensor-data inhibit releases only after the configured count.
3. Start a deliberately low-demand, no-load controller run only if approved; record normal SSR/contact output behavior.
4. Stop temperature-update delivery while keeping the controller powered and the profile active. Do not disconnect safety hardware or alter output wiring.
5. Measure time from the final accepted aggregate/update to SSR-control inactive and contactor-control inactive. Capture coordinator logs showing expiry and pause.
6. Continue observing beyond one PID interval; verify neither output reasserts and that no later command resumes heating without the configured fresh recovery sequence.
7. Restore temperature updates, verify the required consecutive valid aggregate count, and confirm only the intended recovery path resumes operation.

## Pass criteria

- SSR and contactor control reach schematic-defined inactive levels within the approved stale-timeout plus timer/GPIO latency budget.
- Output-off request occurs without a dispatcher/HMI dependency and no output reasserts during the silent-input interval.
- Coordinator records/enters paused state without advancing profile/soak time during the sensor-data pause.
- Recovery requires the configured consecutive fresh aggregates and does not clear a separately induced permanent heater inhibit.

## Abort and recovery

Use the independent emergency disconnect immediately if either output remains active unexpectedly, polarity differs from the schematic, timing exceeds the approved limit, or output reasserts during silence. Preserve traces/logs, return to a verified de-energized state, and do not repeat until safety review resolves the discrepancy.

## Evidence to retain

Firmware/config identity, schematic reference, bench setup, aggregate/update timestamps, logic traces, logs, measured latency, recovery sequence, operator, result, and all deviations.

## Limitations

This plan does not validate chamber thermal response, full Modbus fault handling, power-loss/reset behavior, F-003 stale-command behavior, F-055 dispatcher shutdown, F-056 device teardown, or external safety devices.

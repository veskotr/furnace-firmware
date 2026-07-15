# F-017 powered-controller validation plan

- **Status:** planned; not authorized or executed
- **Firmware identity:** record commit, `sdkconfig` hash, binary SHA-256, and compiler/ESP-IDF version before testing.
- **Level:** powered-controller bench; powered-furnace validation is out of scope.

## Claim under test

After an SSR GPIO operation fails, the heater component latches local output inhibition, zeros demand, retries SSR-low, directly requests contactor-off, and rejects later heater-on commands. Software tests cannot establish GPIO polarity, electrical independence, or physical off latency.

## Prerequisites and controls

- Schematic-reviewed SSR and contactor pin polarity, wiring, and independent external cut-off.
- Qualified operator, isolated controller bench, clear emergency disconnect, and no furnace load.
- Logic analyzer or isolated voltmeter on SSR-control and contactor-control signals; record timestamps.
- Safe initial state verified: contactor de-energized, SSR control low, target demand zero.
- An approved, reversible way to inject the GPIO-driver failure without bypassing electrical interlocks.

## Procedure

1. Record firmware identity and confirm normal inactive output levels with no heating command.
2. Exercise a low-demand controller run without a thermal load; confirm expected SSR window/contact output behavior and abort if polarity differs from the schematic.
3. Inject an SSR-low write failure while the SSR-control signal is logically on.
4. Measure the retry to SSR-low, contactor-off transition, and whether either signal reasserts while the controller remains powered.
5. Attempt normal `SET_POWER`, `START`, and heater-on toggle commands through the approved interface; verify they are rejected and outputs remain inactive.
6. Record the emitted error event/log and any contactor-off failure indication.
7. Remove the injected failure, restart the controller, and verify only the intended restart behavior clears the local latch.

## Pass criteria

- The fault path requests SSR-low and contactor-off without waiting for dispatcher/HMI activity.
- The contactor output reaches the schematic-defined off level within the approved electrical latency budget.
- No later command reasserts SSR/control contactor output before restart.
- Logs/event identify the heater GPIO fault; a contactor-off failure is visible distinctly in logs.

## Abort and recovery

Immediately use the independent emergency disconnect if any output remains active unexpectedly, polarity is uncertain, measurements disagree with the schematic, or the contactor does not release. Preserve logs/traces; inspect wiring and do not repeat without safety review.

## Evidence to retain

Firmware identity, configuration, test setup photos/schematic reference, signal traces, injected-fault method, command log, event/log capture, measured latency, result, operator, and unresolved observations.

## Limitations

This plan does not validate furnace thermal behavior, sensor faults, dispatcher deadlock, pause races, brownout, watchdog reset, or external safety-device behavior.

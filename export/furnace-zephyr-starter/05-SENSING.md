# 05 — Temperature sensing: legacy behavior, MS9024 history, lessons

Sensor problems are a top priority for the new project. Field errors have already come from sensor loss (D-07). The new sensors are likely custom thermal sensors (D-10); their hardware and interface are deferred (T-07). Use the integration-pending rule ([01](01-RULES.md) §3) until they are specified.

## 1. Legacy acquisition chain (final behavior)

1. **Bus:** RS-485 half-duplex, Modbus RTU, 9600 8N1, 300 ms response timeout, no application retries. Only one task used the bus at runtime.
2. **Sensors:** five MS9024 transmitters at slave addresses 1–5. Each poll reads the two-register float process value.
3. **Polling:** about 1 s cycle, sensors read one after another with 100 ms gaps. A timed-out sensor adds 300 ms and keeps its old cached value and old timestamp.
4. **Per-sensor cache:** last value, the tick of the last *successful* read, and a has-sample flag. It was written by the polling task and read by the processing task **without synchronization** (F-071).
5. **Aggregation:**
   - a sample counts only if its last successful read is younger than 5 s;
   - aggregate = arithmetic mean of the fresh samples;
   - valid only if fresh count ≥ N − 2 (3 of 5);
   - disagreement beyond a threshold was **only logged** (F-004), which D-07 replaces.
6. **Freshness enforcement:** the controller re-armed a one-shot 5 s timer on each valid aggregate. Expiry, or an invalid aggregate, inhibited the heater directly and paused the run. Recovery needed three consecutive valid aggregates, but the run never resumed by itself (F-060), and the operator was not told (F-059).

## 2. Value acceptance rules used (keep the idea, re-derive the values)

A reading was rejected when:

- the transport failed;
- the value decoded to NaN or Inf;
- the raw pattern was `0x80000000` (−0.0, the transmitter's "no measurement yet");
- the value was outside −200 … 1500 °C. That range is far wider than the 20–170 °C process, so it catches only gross faults.

The legacy controller also treated an exact `0.0` average as invalid by mistake (F-069). **Never encode validity in the float value itself.**

## 3. MS9024 history (historical: D-10)

| Register (protocol address) | Meaning as used | Healthy reference unit |
| --- | --- | --- |
| 26 | input filter | 50 |
| 27 | unknown (suspected offset/linearisation) | 0 |
| 28 | sensor type (16 = Pt100 α385) | 16 |
| 30 | unknown config | 2 |
| 32 | wiring (4 = 4-wire) | 4 |
| 107–121 | apparently per-unit calibration table | unit-specific |
| 126 | firmware version (used as the bus probe) | 101 |
| 127 | device ID (equals the slave address) | 3 |
| 129 | unknown config/calibration | **265** |
| 447 | °C / °F select | 0 = °C |
| 524 / 726 / **728** / 730 | input offset / analog output / **process value** / cold junction, each a two-register float | 0.0 / 4.32 mA / ~19.9 °C / 0.0 |

- **Float word order "CDAB":** `raw = (second_register << 16) | first_register`, then reinterpret the bits as an IEEE-754 float. This was established empirically only (plausible values, field control worked). It was never checked against the datasheet.
- Odd registers in the 510–540 float area fail when read singly.
- **What happened:** one unit read with an offset. A full register scan was diffed against a good unit (differences in regs 26, 27, 30 and 129), and a hard-coded "repair" table was written.
  - That repair once ran automatically whenever the first read failed (even on a bus warm-up timeout). It was disabled because a timeout is not evidence of corruption.
  - A later healthy-unit dump showed the table's reg-129 value (282) was wrong; the healthy value is 265. **The wrong value was never corrected** (F-066, not being fixed per D-10).
  - The registers involved look unit-specific, so copying them between units can damage calibration.
- A read-only bus-scan and register-dump tool existed but was not started in production.

## 4. Lessons for the new sensing design

1. **One immutable sample record** (value, monotonic timestamp, status, sensor identity), published atomically (R6). No shared value/timestamp pairs.
2. **Freshness is enforced by time, not by the arrival of the next message.** Expiry de-energizes directly (R4, R7).
3. **Outlier exclusion** (D-07, R19): a sensor that differs drastically from the others is excluded from the aggregate and the operator is notified. The algorithm and thresholds are deferred (T-05): reference (median?), absolute or relative deviation, minimum sensor count, re-inclusion, interaction with quorum.
4. **Loss handling is a first-class operator state.** Report it immediately, with which sensors and how many remain. Whether the run resumes automatically is deferred (T-05).
5. **Validity travels with the value** to every consumer, including the display and the fan logic. A stale value is never shown as current.
6. **Commissioning tools are read-only by default** (R16). Any device write is explicit, per unit, verified, and checked against that unit's stored baseline.
7. **Per-sensor statistics:** error counts, last-good age, exclusion count. Expose them on the service console.
8. **Unknown interface → integration marker** (D-14): until T-07 is answered, define a `sensor_source` interface and fakes. Mark the driver `TODO(INTEGRATION T-07)` and invent no bus, scaling or calibration.

## 5. First sensing tests (independent of the sensor hardware)

- Freshness boundary (just under, at, over the budget), using a fake clock.
- Quorum boundary after exclusions.
- Outlier detection (provisional thresholds marked until T-05 is answered).
- An exact `0.0 °C` valid reading is accepted.
- Expiry with no further messages de-energizes before any notification.
- Recovery sequence according to the T-05 answer (provisional until then).

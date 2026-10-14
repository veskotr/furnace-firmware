# 10 — Configuration lessons and precision inventory

The legacy configuration (about 170 project symbols) is **not carried over** (D-15). The values that matter are recorded as a note in [04](04-CONTROL-ALGORITHMS.md) §7. This document keeps what the configuration taught.

## 1. What went wrong with configuration (legacy F-067, F-049, F-073)

At hand-over, three different configurations existed, and none was "the" configuration:

| Configuration | Status |
| --- | --- |
| Kconfig defaults in the code | What a fresh configuration gets |
| Tracked defaults file | Copied from one reviewer's machine and documented as "the reviewed, authoritative configuration" |
| Field build | An **untracked local file** on another developer's machine: the one actually tuned and used in the field |

They differed in 32 project values:

- PID gains, output limit and feedforward;
- overshoot threshold, stall window and fan thresholds;
- log sizes;
- flash size (2 MB versus 8 MB) and the partition table.

Building from the repository produced firmware with none of the field tuning and without the storage partition that crash logs needed. A storage layout change could also trigger a boot-time path that erased all settings, including service hours.

Rules derived from this (see [01](01-RULES.md) R13, R14 and §3):

- One tracked configuration per board/furnace variant. CI builds exactly what ships. Boot prints the configuration identity, and crash records store it.
- No untracked local configuration may differ from the repository.
- The partition/storage layout is part of the variant and is checked in CI. Layout for 16 MB flash: T-12.
- Unknown values are `*_PLACEHOLDER` symbols, rejected by the production-build check. Nobody guesses plausible-looking numbers.
- Configuration help text must agree with the values used. Legacy help texts contradicted the values (for example, help said "typical Ki 5…30" while 1 was used).
- Settings that operators or service staff can change at runtime (possibly PID values, T-03) are versioned persisted settings with migrations, not build options.

## 2. Precision inventory (D-11 accepted; values deferred to T-08)

| Quantity | Legacy resolution / value | Question for T-08 |
| --- | --- | --- |
| Program target temperature | 1 °C (integer) | 0.1 °C? |
| Ramp rate | 0.1 °C/min (stored ×10) | Finer? |
| Stage time | 1 minute | Seconds? |
| Phase / cooling tolerance | 2 °C (whole degrees) | Tighter, or replaced by explicit stage kinds (R9)? |
| Ramp→hold handover tolerance | 0.1 °C steps, 0.5 °C used | |
| Guard thresholds | 1 °C steps | |
| PID gains | 0.001 steps | Float configuration? |
| PID output limits, feedforward points | 1 % steps | |
| Sensor value | float32; instrument accuracy chain unknown | Depends on the new sensors (T-07) |
| Aggregate | Arithmetic mean of fresh sensors | Median / trimmed mean, given outlier exclusion (T-05)? |
| PID period | 1000 ms | |
| Actuator | 5000 ms window, 10 ms resolution (0.2 % duty steps), demand sampled once per window | Window length, mid-window updates (T-14) |
| Sensor poll period | ~1000 ms | (T-07) |
| Time base | 10 ms tick; elapsed time in 32-bit ms (wraps after 49.7 days) | 64-bit uptime |
| Displayed temperature | 0.01 °C formatting | (T-01) |

Until T-08 is answered, implement these quantities with **named units and types in one place** (for example a `units.h` with fixed-point or float types and conversion helpers). Tests then pin the behavior, and changing precision later stays a contained change.

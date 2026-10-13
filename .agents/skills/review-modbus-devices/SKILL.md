---
name: review-modbus-devices
description: Review Modbus RTU transport, device abstractions, MS9024 conversion, and temperature acquisition validity. Use for communications, sensor lifecycle, retry/timeout, malformed responses, stale values, register/unit changes, or reconnect behavior. Do not use for generic networking or PID/profile logic beyond the input contract.
---

# Review Modbus and Devices

## Inputs

Require device/register specification if available, transport configuration, symptom/change, expected units/ranges, and failure policy.

## Workflow

1. Trace request construction, UART/RS-485 ownership, timeout/retry, response/CRC/frame handling, register conversion, and units.
2. Trace device create/init/state/update/read/destroy and concurrent table/cache access.
3. Follow success/failure into validity, freshness, events, quorum, reconnect, and control consumers.
4. Test malformed/partial/late responses, repeated timeout, address/device mismatch, NaN/Inf, endianness, and old-cache behavior by inspection or safe mocks.
5. Separate ESP-Modbus guarantees, physical-device specifications, and unverified assumptions.

## Required output

Report protocol/device flow, ownership, exact evidence, retry/timeout behavior, validity/freshness outcome, findings/confidence, tests, hardware bench needs, and uncertainty.

## Stop and safeguards

Stop when a missing datasheet/library guarantee blocks a conclusion. Do not treat a cached numeric value as valid, write device registers during review, invent protocol behavior, claim mocks prove wiring, or edit production code. Update findings/maps when authorized; verify conversions against primary device documentation when available.

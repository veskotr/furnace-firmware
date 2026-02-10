# Project Plan

## Scope
ESP-IDF firmware for ESP32-DevKitC V4 with a Nextion NX1060P101-011R-I display. The ESP32 owns all logic: UI navigation, validation, program planning, and telemetry except storage. Nextion only reports events and shows data.

Storage is in the SD card on the Nextion.

## Global Rules
- Internal temperature values are stored in Celsius.
- UI temperature scale is display-only; input values are converted to Celsius on receipt.
- All strings are centralized in main/strings.c (no hardcoded string literals).
- UI dimensions are configurable in main/app_config.h.
- Navigation is ESP-driven; Nextion does not change pages directly.

## Milestones & Tasks

### M1: Foundation (current)
- [x] Project scaffold (CMake + main entry)
- [x] UART + Nextion transport stubs
- [ ] Centralized strings
- [x] UI event parser and dispatch layer
- [x] Data models (Config, UserConfig, Program, Stage)
- [ ] Storage layer (NVS)

### M2: Main Page Contract
- [ ] Program browser list fetch and selection event
- [ ] Planned graph rendering (graphDisp)
- [ ] Live graph rendering
- [ ] Telemetry updates (timeElapsed, timeRamaining, currentTemp)
- [ ] Start / Pause / End program state machine
- [ ] ESP-driven navigation (settings, programs)

### M3: Settings Page Contract
- [ ] Read-only cfg_* display values
- [ ] tempScaleBtn conversion and display tracking
- [ ] Editable usr_* values with validation
- [ ] Date/time inputs and validation
- [ ] saveSpecsB payload and save flow
- [ ] websiteQr set on page load

### M4: Programs Page Contract
- [x] Stage table parsing (bStg, t, targetTMax, tDelta, tempDelta)
- [x] Auto-calculation (solve delta T only; delta t fixed)
- [x] Paging (bPrevP, pageNum, bNextP) for 15 total stages
- [ ] Graph preview show/hide and rendering
- [x] saveProgBtn validation and persistence
- [ ] SD save via Nextion FileStream (fs0)

### M5: Error Display Module (placeholder)
- [x] Error message formatting and display routing

### M6: Time Sync Module (later)
- [ ] Wi-Fi time sync

## Open Items
- Confirm safe ranges for user overrides (usr_*).
- Confirm sensor algorithm timing constraints.
- Confirm hardware I/O mapping for furnace control.
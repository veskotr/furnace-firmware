# 07 — What the Nextion HMI does today (and what must be re-homed)

The new controller will not host the Nextion. The `nextion_hmi` component (5 531 lines of C; see [../codex/REPOSITORY_MAP.md](../codex/REPOSITORY_MAP.md)) is not only a display driver: it also carries **domain logic and persistence**. Every row in §2 and §3 needs an explicit owner in the new system.

Panel-side design notes and the vendor instruction set are in [../../components/nextion_hmi/docs/](../../components/nextion_hmi/docs/). The `.HMI` panel project source is not in this repository (F-046).

## 1. Operator functions (the inbound command set)

Lines arrive as ASCII from the panel. They are cleaned of non-printables and routed by `nextion_events.c:nextion_event_handle_line`. Routing is substring-based except for the destructive commands, which require an exact match (F-032).

| Group | Command (panel → controller) | Effect | Allowed while a program runs |
| --- | --- | --- | --- |
| Run | `prog_start` | Validate the current draft for run (`program_validate_draft_for_run`), then `COORDINATOR_START_PROFILE` with the draft plus the stored cooldown rate | yes |
| Run | `prog_pause` | Toggles pause/resume based on the HMI's own `s_profile_paused` | yes |
| Run | `prog_stop` → `confirm_end` | Confirmation dialog, then `COORDINATOR_STOP_PROFILE` ("can't resume") | yes |
| Manual | `manual_toggle`, `manual_temp_inc/dec`, `manual_delta_inc/dec`, `manual_temp_set:<int>`, `manual_delta_set:<d.d>` | Manual target 20–170 °C, rate 0.1–3.0 °C/min; start builds a one-stage program (F-064); live changes send `UPDATE_MANUAL_TARGET` | yes |
| Fan | `fan_mode_toggle` | Toggles stored Max/Silent preference; **not applied** (F-070) | yes |
| UI | `err:close` | Dismiss error overlay | yes |
| Navigation | `nav:main`, `nav:programs`, `nav:settings` | Page change plus refresh | no ("Stop program first") |
| Programs | `prog_select:<file>`, `prog_page_data:<…>`, `add_prog`, `edit_prog:<…>`, `prog_page:prev/next`, `save_prog:<…>`, `delete_prog:<name>` → `confirm_delete`, `prog_back:<dirty>`, `show_graph:<…>`, `autofill:<…>`, `prog_field:<…>` | Program browser and editor; save-time validation (`program_validate_draft_with_temp` from the stored ambient reference); autofill; projected-curve graph | no |
| Settings | `settings_init`, `save_settings:<12 comma tokens>` | Time/date (written to the **panel RTC** `rtc0..rtc5`), ambient reference temperature (0–170), cooldown rate | no |
| System | `restart`, `factory_reset` → `factory_reset_confirm` | Inhibit the heater first, then reboot; factory reset deletes registered programs, erases NVS preserving operating hours, reboots | no |

## 2. Domain logic inside the HMI component

| Function | Where | Notes for the rewrite |
| --- | --- | --- |
| Program draft model (create/edit stages, set-flags) | `program/heating_program_models.c` | Move to a core library with tests |
| Save-time validation, autofill | uses `components/heating_program_validation` | That component is already HMI-independent, but its limits come from `CONFIG_NEXTION_*` (F-076) |
| Run-start validation | `nextion_run_handlers.c:handle_run_start` | **Must move into the controller.** Today the coordinator trusts the HMI to validate. |
| Projected curve and duration estimate | `program/heating_program_graph.c`, `program_calculate_stages_duration_ms` | A second copy of the duration math, aligned with `calculate_program_duration_ms` by convention only. The graph uses linear `t_min` interpolation (diverges from runtime easing) |
| Remaining-time-during-pause extension | `nextion_run_handlers.c` (`s_pause_extra_ms`) | Belongs to the run state, not the UI |
| Manual-mode program construction | `nextion_manual_handlers.c:start_manual_program` | Replace with a real manual mode (MAN-01) |
| **Operating-hours counter** (1 s while a program is active, including pause) | `nextion_run_tick` → `program_add_operational_time_sec` | Service record; must live in the controller |
| Power display in kW = output × `NEXTION_HEATER_POWER_KW` | `nextion_event_handle_status_update` | Nominal, not measured |
| Machine-state text (`S2/5 RAMP 150C`, `HOLD … 10m`, `COOL`, `Cooldown`, `Completed`, `Paused`) | `format_machine_state` | The operator vocabulary to preserve |
| Fault text ("Heating stall S2: 120/150C after 5min", "Hold off-target …") | `nextion_event_handle_profile_error` | Preserve the content; add over-temperature and sensor-loss messages (F-058/F-059) |

## 3. Persistence owned by the HMI path

| Data | Store | Key / format | Notes |
| --- | --- | --- | --- |
| Ambient reference temperature (°C) | ESP NVS `user_prefs` | `ambient_c` (i32) | Used as the first stage's start temperature in save-time validation |
| Operating hours (s) | ESP NVS `user_prefs` | `op_time_s` (u32) | Written at most every 60 s and on stop/complete; preserved by factory reset; lost by the boot-time NVS erase (F-073); service override (F-050) |
| Fan mode | ESP NVS `user_prefs` | `fan_mode` (u8) | Display only (F-070) |
| Cooldown rate (x10) | ESP NVS `user_prefs` | `cool_rate` (i32) | Used for estimates only |
| Program-name registry | ESP NVS `prog_registry` | `count` (u32), `p000`…`pNNN` (str) | Needed because the panel has no serial directory listing; used for factory-reset deletion (F-026) |
| Program library | Nextion panel micro-SD | `sd0/<name>.prg`, ≤ 4096 B, written via `twfile` (temp file then `refile`), read via panel FileStream | Text format below |
| Wall-clock time/date | Nextion panel RTC | `rtc0..rtc5` | **The controller has no wall clock** (F-068) |

Program file format (`nextion_storage.c:nextion_serialize_program` / parser):

```
name=<program name>
stage=<1-based index>,t=<t_min>,target=<°C>,tdelta=<t_delta_min>,delta_x10=<rate x10>
...
```

The parser also accepts a legacy `delta=` key. Existing field programs are stored in this format on the panel SD cards. Decide whether the new system must import them.

## 4. Outbound display contract (controller → panel)

Status per PID tick: temperature, setpoint, power 0–1, elapsed ms, total ms, stage ordinal / total, phase, stage remaining ms (`coordinator_status_data_t`). Lifecycle events: STARTED, PAUSED, RESUMED, STOPPED, COMPLETED, ERROR (with code, esp_err, temperature, setpoint, stage, fault duration). The temperature display uses the float event, posted only when the aggregate is valid.

## 5. Re-homing checklist for the new project

Decided 2026-10-13 ([10](10-DECISIONS-AND-DEFERRED-TOPICS.md)):

- **D-01:** every responsibility above moves to the controller; the new display is only a remote graphical interface.
- **D-02:** a custom protocol will be designed for both.
- **D-03:** the operator interface runs on the new HMI board, and the program library lives in controller memory.

Open: T-01 (status/notification design), T-02 (protocol), T-13 (data migration).

- [x] Choose where the operator UI lives (Q-09): the new HMI board, a remote GUI over a custom protocol (D-01/D-02/D-03). Protocol design is deferred to T-02.
- [ ] Define a versioned controller API (commands, state, telemetry, faults) and test it with a fake client. The HMI is a client of that API, never a validator.
- [ ] (decided D-03; design pending) Move program storage and validation into the controller (or a defined service), with atomic replace and a migration/import path for existing `.prg` files.
- [ ] Move the operating-hours counter, ambient reference and cooldown preference into controller-owned persistent settings, with explicit migration from NVS `user_prefs`.
- [ ] Provide a wall-clock source (RTC, NTP or HMI-supplied time) for logs and records.
- [ ] Preserve the operator vocabulary of §2 and add the missing fault states (over-temperature abort, sensor loss, permanent heater fault).

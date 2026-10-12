# Central ESP32 analysis baseline

Date: 2026-09-14  
Status: baseline/component inventory complete; A–F and cross-component source
reviews integrated. Field/hardware evidence remains open.  
Scope: [preparation requirements](ESP32-ZEPHYR-PREPARATION.md) and
[execution plan](ANALYSIS-EXECUTION-PLAN.md).

## Identity and preserved worktree

| Item | Captured evidence |
| --- | --- |
| Repository | `/home/vesko/projects/furnace-firmware` |
| Branch | `audit-docs-verification` |
| HEAD | `1fcb4b1115ce9560fe2c4944ebc868cb454e5ed5` — Finalize firmware hardening closeout |
| Source/context snapshot window | 2026-09-14 11:56:45–11:58:34 UTC |
| Initial tracked changes | Six unstaged documentation paths; no staged paths. |
| Initial untracked files | 23 documentation files; no application source additions. |
| Tracked application source | Matches HEAD; retained untouched throughout this step. |
| Field deployment | User confirms ESP-IDF firmware operates an oven. On 2026-09-15 the user said its settings match the `field-tests` branch; exact flashed source revision and saved resolved configuration remain unverified. |

The initial modified paths are README, CURRENT-FIX-STATUS, FIRMWARE_FINDINGS,
both repository maps, and the decisions README. Their exact paths and all
initial untracked documents are retained in the
[snapshot JSON](baseline/2026-09-14-snapshot.json). No reset, stash, checkout,
commit, or production-code/configuration change was performed.

Two read-only repository reviewers checked component classification and
build/configuration provenance in parallel. The lead captured the snapshots,
reconciled their evidence, and integrated documentation. A usage-limit
interruption was resumed; both assignments completed. These were baseline
assignments, not the A–F specialist behavior reviews.

## Reproducible comparison boundary

- [Source/configuration manifest](baseline/2026-09-14-source.sha256): 172 files.
  Includes 59 C files, 54 headers, 25 component/main CMake files, 27 Kconfig
  files, and the main component manifest, plus six root build/configuration
  files. Dormant/excluded source is included; generated/vendor source is not.
- [Initial context manifest](baseline/2026-09-14-context.sha256): 101 selected
  instruction, analysis, map, agent/skill, and documentation-tool files before
  baseline documentation integration. This is historical context evidence;
  subsequent intentional documentation edits will change those hashes.
- [Stored artifact manifest](baseline/2026-09-14-artifacts.sha256): 14 build,
  dependency and partition files. These fingerprints identify existing local
  evidence; they do not preserve missing artifacts or reproduce a build.

From repository root, verify source/configuration drift before starting reviews:

```sh
sha256sum --check --quiet docs/analysis/baseline/2026-09-14-source.sha256
git status --short --untracked-files=all
```

The manifest detects content changes or removal of captured files. It does not
detect newly added files; repeat the component/source enumeration when status
or build definitions change. If only expected documentation changed, continue
against the same source baseline. Investigate source/configuration drift and
update the reviewed baseline rather than silently combining revisions.

## Configuration and dependency authority

`git ls-files` and `git check-ignore -v` establish that `sdkconfig.defaults`,
`dependencies.lock`, `main/idf_component.yml`, and `partitions.csv` are tracked.
[.gitignore](../../.gitignore) ignores `build/`, `sdkconfig`, and `sdkconfig.old`.
Descriptions of a "tracked sdkconfig" in older audit prose are inaccurate.

Local resolved `sdkconfig` is byte-identical to tracked `sdkconfig.defaults`:
SHA-256 `4d3b78c464fbc275b40fdbc9a6540f9281f895e6a347ea07ee8f2cc431af151b`.
The build reviewer compared all 1,499 non-deprecated resolved symbols with
`build/config/sdkconfig.json`: 987 values/enabled symbols and 512 disabled
booleans agree after hexadecimal normalization. Deprecated compatibility aliases
were excluded. This is current file/metadata agreement, not clean-build proof.

`sdkconfig.old` differs materially: it describes a single heater GPIO25 and
10-second window, older PID gains and enabled run indicator. It is historical
local state, not the current configuration authority.

Deployment clarification (2026-09-15): local `field-tests` points to
`243a15a53bd2d7a5b72478dc9283db605d892129`, an ancestor of this baseline.
That branch does **not** track `sdkconfig` or `sdkconfig.defaults`; the latter
was added after it. The user's statement that oven settings match that branch
therefore cannot be independently checked from its Git tree. It narrows the
intended settings provenance but does not prove the local resolved values above
are the flashed values. This HEAD changes 51 application/build paths relative
to `field-tests`, so source-level findings for HEAD must not be reported as
observed behavior of an unknown flashed revision. A saved field `sdkconfig`/
build record or reported image hash would close that gap without operating the
oven during this analysis.

Targeted source comparison found the F-070 live-cooling trigger already in
`field-tests`: manual target update can change phase without stage advance,
while coordinator off is conditioned on `stage_changed` and cooling skips PID.
This is branch-source evidence only; no flashed-image or physical-output
observation follows from it.

[main/idf_component.yml](../../main/idf_component.yml) requests IDF `>=4.1.0`,
LittleFS `==1.20.4`, and ESP-Modbus `^2.1.2`.
[dependencies.lock](../../dependencies.lock) resolves IDF 5.5.4, LittleFS 1.20.4,
ESP-Modbus 2.1.2, target ESP32. Both present managed component manifests require
IDF `>=5.0`. Their version/hash markers agree with the lock; their complete
vendor contents were not integrity-audited. The manifest's broad IDF constraint
does not prove compatibility with all matching SDK versions.

## Selected configuration to carry into reviews

Values below are from local `sdkconfig` and its identical tracked defaults.
They describe selected settings, not approved cure limits or measured hardware.

| Area | Selected values | Source/use-site navigation |
| --- | --- | --- |
| Platform | ESP32/Xtensa, IDF 5.5.4, dual-core FreeRTOS, 100 Hz tick, CPU 160 MHz | `sdkconfig` platform/FreeRTOS sections; [main](../../main/main.c). |
| Outputs | Contactor GPIO22, SSR GPIO21, SSR window 5,000 ms; fan GPIO19, auto on/off 35/32°C | [heater Kconfig](../../components/heater_controller_component/Kconfig), [fan Kconfig](../../components/fan_controller_component/Kconfig). |
| Modbus | UART2 TX27/RX26/DE25, 9,600 baud; RTU 8N1 half duplex, explicit 300 ms response timeout | `app_main` configuration; [modbus_master_init](../../components/modbus_master/src/modbus_master_core.c). |
| Sensor population | Startup creates 5 sensors, starting address 1; ordinary first-init quorum is 3 of 5. Processor capacity 9, buffer 10, disagreement warning 2°C | [processor initialization](../../components/temperature_processor_component/src/temperature_processor_core.c), [aggregation worker](../../components/temperature_processor_component/src/temperature_processor_task.c), [sensor creation](../../components/temp_sensor_device/src/temp_sensor_device_core.c). |
| Scan scheduling | Manager nominal period 1,000 ms plus sequential updates and 100 ms delay per running device | [device_manager_task](../../components/device_manager/src/device_manager_task.c). Nominal period is not a proven fixed batch duration. |
| Temperature authorization | Aggregate expiry 5,000 ms; recovery count 3; automatic recovery enabled | [coordinator Kconfig](../../components/coordinator_component/Kconfig), [temperature event/expiry handling](../../components/coordinator_component/src/coordinator_component_events.c). |
| Profiles | PID tick 1,000 ms; completion temperature 40°C; overshoot threshold 30°C; ease band 6°C; handover tolerance 0.5°C | [coordinator Kconfig](../../components/coordinator_component/Kconfig); precise semantics belong to package C. |
| Control guards | Stall check 180 s, minimum lag 5°C, minimum rate fraction 50%; hold deviation band 4°C for 300 s | Same coordinator settings and use sites; existing field acceptance remains unknown. |
| PID | Integer gains 115/1/0 scale to Kp 0.115, Ki 0.001, Kd 0; output 0–1; adaptive output limit/feedforward disabled | [PID implementation](../../components/pid_component/src/pid_component.c). |
| HMI | Enabled, UART1 TX32/RX33, 115,200 baud, RX/TX buffers 2,048 bytes; service-time override disabled | [HMI Kconfig](../../components/nextion_hmi/Kconfig) and nested settings; current Nextion behavior remains the migration input. |
| Task/queue budgets | Dispatcher queue 10, event queue 32; both stacks 4,096/priority 5. Coordinator/heater/device/processor stacks 4,096/priority 5. HMI coordinator stack 8,192/queue 8/priority 5, RX stack 4,096/priority 10 | Component Kconfig and resolved configuration. Values are configured budgets, not high-water measurements. |
| Logging | Queue 64, message bound 64, ring 100, panic buffer 20; task stack 4,096/priority 4; ESP log default/max INFO | [logger Kconfig](../../components/logger_component/Kconfig), [logger core](../../components/logger_component/src/logger_core.c). |
| Watchdogs/reset | Boot WDT 9 s; interrupt WDT 300 ms, CPU1 checked; task WDT initialized at 5 s with idle tasks checked and panic disabled; brownout enabled, level selector 0; panic print/reboot, zero delay | SDK settings in resolved config. Application health-monitor initialization is commented out in main; that does not mean all SDK watchdogs are off. |
| Memory diagnostics | PSRAM off; stack canaries on; heap poisoning/tracing/task tracking and abort-on-allocation-failure off; ESP coredumps off | SDK settings in resolved config; runtime memory use remains unmeasured. |

Existing F-049 configuration drift remains relevant: component defaults include
overshoot 20°C, stall 300 s, integer Kd 30, and fan thresholds 43/40°C, while the
selected values above differ. No configuration was changed to reconcile them.

## Storage and existing build evidence

The selected flash configuration is 2 MiB, DIO/40 MHz, standard single-app table.
`build/flasher_args.json` agrees: bootloader at `0x1000`, partition table at
`0x8000`, app at `0x10000`. The stored partition binary contains NVS at `0x9000`
(size `0x6000`), PHY at `0xf000` (size `0x1000`), and one factory app at `0x10000`
(size `0x100000`). It has no `storage` partition.

The unselected repository [partitions.csv](../../partitions.csv) ends at 8 MiB
and includes `storage`. [logger_init_storage](../../components/logger_component/src/logger_storage.c)
requests that literal label; [logger_init](../../components/logger_component/src/logger_core.c)
calls it and logs failure while continuing. This revalidates the configuration
boundary of existing F-068; no mount or field behavior was observed.

Stored `build/project_description.json` names project version `1fcb4b1`, SDK
`v5.5.4`, ESP32, local resolved configuration and tracked defaults. The existing
application binary embeds the same version/SDK and build date Aug 4, 2026;
size is 408,752 bytes (`0x63cb0`). Its SHA-256 is in the artifact manifest.
Prior audit notes report a successful build of this revision; this step did
not rebuild it or establish complete provenance of every object.

The component reviewer found 55 local C compilation-command entries and 17
local components/main entries with extracted objects in the stored map. The
[coverage index](ANALYSIS-COVERAGE.md) distinguishes these from registered,
dormant and excluded components.

## Toolchain availability and execution limits

The local IDF checkout is `/home/vesko/.espressif/v5.5.4/esp-idf`, described as
`v5.5.4`, commit `735507283d5b2f9fb363a1901172dbd9e847945d`.
The cached compiler runs and reports GCC 14.2.0 / crosstool-NG
`esp-14.2.0_20260121`; CMake 4.2.3 and Ninja 1.13.2 run.

The shell has no `IDF_PATH`, `IDF_PYTHON_ENV_PATH`, or `VIRTUAL_ENV`, and neither
`idf.py` nor the Xtensa compiler is on PATH. The cached Python path
`/home/vesko/.espressif/tools/python/v5.5.4/venv/bin/python3` currently resolves
to Python 3.14.4. `PYTHON_DEPS_CHECKED=1` in an old cache is not a current Python
dependency check. Later build work must initialize/check its environment and
record actual command results; historical fallback commands are not readiness
evidence for this shell.

No project firmware unit/integration harness or test registration was found in
root/main/components/tools. Vendor tests and SDK test facilities are separate.
No firmware configure/build/test, download, Zephyr installation, flashing, or
hardware operation was performed in this step.

## Documentation reconciliation and finding identity

The baseline refresh corrects both repository maps for ignored local configuration
versus tracked defaults, disabled/discovered indicator versus excluded legacy,
the configuration-only time component, common headers, binary exit semaphores,
the expiry timer, and quadratic rather than cubic ramp easing. It does not
reclassify defects or claim their regression validation.

Some older audit notes contain provisional IDs that differ from the central
[findings register](FIRMWARE_FINDINGS.md). Use the register's IDs during reviews:

| Old audit label | Central ID | Meaning |
| --- | --- | --- |
| Audit 005 F-061 | F-061 | Coordinator STOP wake permits one final control tick. |
| Audit 006 F-061 | F-062 | Dispatcher teardown does not quiesce external producers. |
| Audit 006 F-062 | F-063 | Sensor destruction/reuse overlaps manager update. |
| Audit 007 proposed F-063 | F-064 | Debug-console shutdown lacks worker join. |
| Audit 007 proposed F-064 | F-065 | Disabled health-monitor worker outlives teardown. |
| Audit 007 proposed F-065 | F-066 | Disabled health-monitor partial-init cleanup. |
| Audit 007 proposed F-066 | F-067 | Excluded legacy temperature-monitor lifetime defects. |

Preserve original audit history; D/F will reconcile its prose during their
reviews. F-003's register entry records a source fix with validation pending,
while some residual-risk paragraphs/maps still call it open or uncorrected.
A/C/D must reconcile the precise current contract before changing that status.
Older "indicator excluded from build" and "tracked sdkconfig" wording elsewhere
is likewise a documentation correction queue, not a new firmware finding.

## Remaining inputs and next step

Field revision/configuration, actual flash/board/sensor identity, independent
cutoff/airflow/E-stop wiring, cure timing/tolerance requirements, and resource
measurements remain open. No missing field fact prevents the next source-only
reviews; it limits the physical or product-policy conclusions they may draw.

The baseline gate is complete. Wave 1 can use the shared source manifest and
coverage index for A (outputs), B (temperature/Modbus), and C (process/PID).
All detailed A–F review and test-design statuses remain pending until those
assignments return and their evidence is integrated.

## Checks executed for this step

| Check | Result |
| --- | --- |
| Source/configuration SHA-256 check | PASS, 172 captured files unchanged at handoff. |
| Initial-context SHA-256 check | PASS before intentional documentation integration; retained as historical snapshot. |
| Stored-artifact SHA-256 check | PASS, all 14 captured artifact/dependency entries match. |
| Coverage names versus captured source/component inventory | PASS, 25 entries and no missing/extra primary component rows. |
| Snapshot JSON and repository-map YAML parsing/consistency | PASS. |
| `python3 tools/codex/verify_repository_setup.py` | PASS, 11 roles/15 skills plus metadata, paths and Markdown links. |
| `git diff --check` | PASS. |

These checks validate the analysis artifacts and unchanged captured inputs.
They are not firmware regression tests or physical validation.

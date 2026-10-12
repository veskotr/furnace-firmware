# BUG-076: Downward cooling target can trigger false runaway completion

- **Subsystem:** profile phase/guard and coordinator emergency completion
- **Severity:** high cure-cycle integrity/control-state defect
- **Confidence:** confirmed source-level validated program trigger
- **Status:** confirmed; correction not authorized
- **Affected files and symbols:** `profile_tick`, `advance_stage`, `program_validate_draft_for_run`, coordinator profile error handling
- **Prerequisite or linked IDs:** F-070 no-heat cooling authorization; F-048 trajectory/display residual; F-052 missing control tests
- **Ready to fix:** no; direction-aware and absolute overtemperature policy/test vector require review

## Observed behavior and reachable trigger

A valid program can transition from a 150 °C heating/hold stage to a 100 °C
cooling stage. Cooling immediately uses 100 °C as its setpoint. The generic
overshoot guard still tests `actual > setpoint + 30 °C`; a chamber at 150 °C
therefore trips at the next cooling tick, reports threshold exceeded, and the
coordinator emergency-completes the run although the chamber is following an
ordinary downward step. If the first runtime phase is cooling, the guard can
trip on its first tick. This is source behavior, not a measured cure run.

## Evidence and disconfirming validation

[Runtime profile](../../components/temperature_profile_controller/src/temperature_profile_core.c)
sets `setpoint=target` in `STAGE_PHASE_COOLING`, then applies the positive
`current_temp > setpoint + CONFIG_COORDINATOR_OVERSHOOT_THRESHOLD_C` guard to
all phases except final COOLDOWN/COMPLETE. The source comment itself notes
direction may need separate handling. [Run validator](../../components/heating_program_validation/src/heating_program_validation.c)
explicitly permits cooling stages and skips ramp/time validation for a lower
target. Resolved threshold is 30 °C; field value is unverified. This is not
the same as F-070's cooling heater-output revocation failure.

## Root cause and violated invariant

The positive setpoint-relative runaway test is applied after a legitimate
downward step, where setpoint intentionally lies below current temperature.
Invariant: phase transition to cooling must not be treated as runaway solely
because actual exceeds the new lower target; a genuine independent absolute
or direction-aware overtemperature response still needs specification.

## Minimal root-fix boundary and safety impact

Characterize heat/hold/cool guard semantics and distinguish legitimate
cooling-direction error from real high-temperature fault, without disabling
any factory ceiling. The false trip requests output-off and interrupts a cure;
it does not by itself prove unsafe heating. The current oven lacks independent
hardware overtemperature protection; software guard changes must not be
treated as a certified safety function. Exact cure tolerance and absolute
ceiling are product-policy gaps.

## Allowed scope, ADR, and correction authority

Documentation/diagnosis only. A control-policy correction requires explicit
patch authority, control/safety impact review and host vectors. An ADR is not
needed for a local directional guard if external fault/state semantics remain
unchanged; escalate if the fault taxonomy or factory ceiling changes.

## Regression test and hardware validation

`SCN-W1-006`: host `profile_tick` vector for a validated 150→100 °C step and
a genuine high-temperature case. Failure before correction: the cooling
vector returns threshold exceeded at 150 °C on the next tick. Passing
correction: ordinary cooling continues with heater authorization revoked,
while the separately specified genuine fault still reports/inhibits.
No host harness or test was implemented/run. Chamber behavior, physical
outputs and cure quality require separate authorized validation; no oven
operation is authorized.

## Resume conditions

Specify directional/absolute overtemperature rules and the host oracle,
then authorize a bounded correction after F-070 output-policy interaction
review. Retain independent production hardware protection as future work.

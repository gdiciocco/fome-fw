# Blended airmass: accepted revision requirements

## Status and scope

Requirements accepted 2026-09-29; implementation status updated 2026-09-30.
Sections 1–10 are implemented in the final candidate `88f07ff8f0` (main revision
`d5459bbb58`), with the restricted endpoint-analyzer scope described in section 6.
The host suite passes 831/831 tests, Core8 and Proteus F7 builds pass, and the
isolated Core8 bench passes all 55 samples. Interactive TunerStudio editor and
cursor checks passed; original flash and configuration were restored byte-for-byte.
Running-engine calibration is not qualified. See the current
[revision validation record](blended-airmass-revision-validation.md) for evidence
and qualification limits. Section 11 remains a study and has no runtime implementation.

The requirements below retain their normative wording and take precedence over
the original [development plan](blended-airmass.md). References to prior behavior
identify the pre-revision baseline. The older
[validation record](blended-airmass-validation.md) documents that baseline; its
hardware results do not qualify this revision.

Keep the three independent SD, Alpha-N and MAF calibrations and the SD + Alpha-N
mass blend. Keep the present map dimensions and encodings. **Table resizing,
larger tables, PR #791 integration and shared map-storage slots are excluded.**

The calculation remains a blend of air masses in grams per cylinder. Model
physics, blending authority and downstream load coordinates are separate
concerns. MAF remains standalone; Lua and test strategy identifiers retain
their established meaning.

## 1. Dedicated maps as the normal configuration

- Remove the user-facing `Dedicated airmass tables` opt-in. Each model always
  reads its own calibration and axes, including dry diagnostic/Lua evaluations.
- Provide selection of SD alone, Alpha-N alone, or SD and Alpha-N together.
  Selecting both enables blending and its authority table. MAF remains a
  separate standalone choice. An empty model selection must not silently
  choose a model, and MAF must not become an implicit third blend participant.
- Remove the main VE load-axis override from the normal tuning workflow.
  SD uses effective MAP, Alpha-N uses TPS, and MAF uses its native uncorrected
  filling coordinate. This removal does not remove existing overrides for
  lambda, ignition or correction tables.
- Preserve existing strategy IDs where possible. The UI may express the
  SD/Alpha-N choices with selectors without requiring redundant firmware state
  that can disagree with the selected strategy.

All three calibrations must remain independently stored when switching modes.
Maintain initial 0% Alpha-N authority for a newly created blended calibration;
selecting both models must not invent or automatically tune the Alpha-N map.

## 2. Remove manual model-readiness declarations

Remove `SD map and MAP load tables ready` (`sdAirmassMapReady`) and
`Alpha-N map ready` (`alphaNAirmassMapReady`) from configuration and activation
requirements. Selecting the operating strategy is the explicit activation step.

Keep automatic checks on map values, axes, authority, required sensors and
calculated mass. Keep the existing fault latch, injection gating and deliberate
recovery contract until separately revised and qualified. Removing readiness
checkboxes does not remove fault recovery or the runtime `Ready` status.

Validation cannot establish that a map is correctly calibrated for an engine.
Document placeholder defaults and the need to calibrate each contributing
model. Do not replace the deleted checkboxes with another declaration of the
same meaning or a heuristic that treats plausible numbers as proof of tuning.

## 3. Names and editor behavior

| Pre-revision presentation | Accepted direction |
| --- | --- |
| `Shared VE / Speed Density VE` | `Speed Density VE`; remove the shared-map interpretation. |
| Alpha-N map | Give it an explicit Alpha-N identity. If labeled `Alpha-N VE`, explain that its percentage represents filling at the model's reference conditions. |
| MAF map | Identify it as a MAF correction; 100% is neutral. |
| Authority map | Identify values as `% Alpha-N contribution`: 0% SD, 100% Alpha-N. |
| `Composite fuel cursor` | Each editor identifies its own selected load source and units; an ambiguous common label cannot stand in for MAP or TPS. |
| `MAP estimate calibrated` | `Use MAP estimate table`, with an actual permission-to-use meaning. |

The models' map percentages have different meanings; help text must explain
those meanings without implying that blending averages the percentages.
Allow preparation of maps before they contribute. Distinguish editability from
a live cursor or analyzer being valid for the current operating mode.

## 4. MAP-estimate selection

Replace the calibration-readiness declaration with an explicit choice to use
the existing TPS/RPM MAP-estimate table. Automatic validation of its contents
and inputs remains necessary. An enabled switch does not certify accuracy.

The owner confirmed the following general permission on 2026-09-29. The existing
`Use MAP estimate during transient` control is subordinate to it:

| Use estimate | Transient use | Required behavior |
| --- | --- | --- |
| Off | Inactive | Use measured MAP where required; do not evaluate the estimate as fallback or for transient comparison. A missing required MAP remains an input fault. |
| On | Off | Permit a valid estimate when measured MAP is unavailable. |
| On | On | Also permit the existing transient comparison when its trigger is satisfied. |

When the general permission is off, the transient control must be shown as
inactive and must not cause the estimate to be used, even if its stored setting
is on. New calibrations default the general permission to off. Migration must
explicitly map old behavior to the new permission rather than treating a field
rename as sufficient conversion.

Keep measured MAP distinct from effective MAP: a consumer explicitly requesting
measured MAP cannot be satisfied by an estimate. Audit the permission's scope
in standalone operation and migration so a renamed control does not silently
change existing fallback behavior.

## 5. Independent load selection for every consumer

This requirement was explicitly confirmed by the owner: **every user of the
blended load gets independent selection**, at least effective MAP in kPa or
TPS in percent. A single global load selector does not satisfy it.

The pre-revision composite published effective MAP as `fuelingLoad`, including
at 100% Alpha-N authority. That diagnostic retains its pressure meaning. The
revision gives consumers independent selectors and publishes their actual
coordinates; no weighted combination of MAP and TPS is introduced.

Before editing call sites, enumerate every direct and indirect consumer and
record its source, units, selector, editor/cursor, thresholds, validity checks,
publication timing and migration behavior. This includes at least:

- injection phase and each cylinder fuel-trim table;
- Idle VE, with its own load-axis selector and explicit model ownership;
- STFT region selection and its load thresholds;
- each VVT target table and other VVT load-dependent logic;
- lambda targets, ignition tables, trims and corrections;
- staged injection, independently of the lambda-table selection it currently
  inherits;
- lambda monitoring, independently of the lambda-target selection;
- VE corrections and configurable GPPWM load consumers;
- other load-based thresholds, control logic and diagnostic paths discovered
  by following `fuelingLoad`, `ignitionLoad` and their aliases.

Existing independent selectors can be reused. Each independently calibrated
table or control consumer must retain its own choice, even if it previously
shared a source with another table in the same subsystem. Preserve additional
source options already supported by existing selectors.

Selecting TPS for one consumer must not change any other consumer, either main
model's axes, or blending authority. Editor axes, units, live cursors and
thresholds must agree with the source actually read by the firmware. Changing
units requires deliberate recalibration; simply relabeling kPa bins as percent
is not a conversion.

Dependency checks must use the union of inputs required by active models,
authority, corrections and consumers. A consumer set to TPS need not require
MAP for its own lookup, but an active SD model can still require MAP. Do not
silently change a consumer's source when a sensor fails. Preserve the established
sampling stage where a correction reads previously published load; avoid a
circular calculation through the final corrected air mass.

Audit diagnostic/protocol outputs separately from tunable lookups: a field
whose external contract specifies pressure or normalized filling must retain
that meaning. A configurable internal table feeding that output still needs
its own load selection. Document these distinctions in the consumer inventory.

## 6. VE Analyze at qualified endpoints

The pre-revision baseline disabled analysis throughout composite mode. The
accepted extension is analysis attributable to a single model: SD at exactly 0% Alpha-N authority,
or Alpha-N at exactly 100%. Preserve the dedicated standalone analyzer bindings,
including MAF, and their existing applicability checks.

Do not use one lambda residual to tune both maps independently in the mixed
region. An endpoint sample must write only its contributing map using that
map's axes. Qualification must verify sample filters, authority stability,
lambda measurement delay, transitions between regions, and the target-lambda
coordinate selected for that consumer. A delayed residual from a mixed region
must not be accepted merely because current authority has reached an endpoint.

Confirm what TunerStudio bindings and filters can enforce before enabling this
extension. Until that evidence exists, retain the current disablement for the
unsupported case. The rusEFI table-blending comparison motivates this review;
it does not establish equivalent air-model physics or analyzer compatibility.

Implemented scope: a composite session can qualify only with an entire authority
table uniformly at 0% or uniformly at 100%, after the startup delay and with
Idle VE disabled. Live configuration/strategy changes and faults invalidate the
session. A zero or invalid RPM sample does not create a new qualified session;
a confirmed stop permits fresh qualification. Moving endpoints and mixed-region
analysis remain unsupported. Host tests cover admission; interactive TunerStudio
filters and delayed measurements still need qualification.

## 7. Idle VE with an independent load axis

The owner added Idle VE support to the revision on 2026-09-29. Make it usable
in SD + Alpha-N operation as well as the supported standalone modes; remove
the pre-revision blanket incompatibility between composite mode and Idle VE only
when the corresponding calculation and validation are implemented.

Keep an explicit enable control and provide an independent Idle VE load-axis
selector, including effective MAP (kPa) and TPS (percent). Preserve or explicitly
migrate existing `idleVeOverrideMode` choices, including the measured-MAP
meaning of the existing MAP override. The Idle VE axis must not inherit another
consumer's selection. Its editor, units, live cursor and required sensors must
follow its own source. Selecting TPS for this axis does not change the owning
air model or eliminate the pressure dependency of SD.

The owner confirmed a single Idle VE table assignable to SD or Alpha-N in
composite mode. Its target-model selector is independent of its load-axis
selector: an SD-owned Idle VE may use TPS bins while still supplying SD VE;
an Alpha-N-owned Idle VE may use MAP bins while still supplying Alpha-N
reference filling. Describe the selected model meaning in the editor.

Apply the idle/main-map interpolation only inside the selected model, before
its mass is blended with the other model's mass. The other model retains its
own main map. If authority gives the selected model zero contribution, Idle VE
has no effect and must not force that branch to run or require inputs solely
for its idle lookup. Keep authority unchanged. Do not apply the table to both
models or reinterpret it as a multiplier of final blended mass.

Switching the target model reassigns this one calibration; it does not convert
the cells between SD VE and Alpha-N filling or preserve two separate idle
calibrations. Require deliberate recalibration for the new model. Preserve
existing supported standalone Idle VE behavior, including MAF, through explicit
migration and regression coverage; the SD/Alpha-N target selector governs the
composite path.

Preserve and document the existing idle/taper activation and TPS-based return
to the main map where applicable. The load-axis selector controls the table
lookup; it does not redefine idle detection, the taper trigger or blending
authority. Specify which model parameter is replaced and how each transition
is interpolated. Apply shared corrections once and preserve the usual authority
semantics at 0%, 100% and intermediate contributions.

When Idle VE contributes, endpoint VE Analyze must not attribute the resulting
lambda residual to a main map that did not fully determine fueling. Reject those
samples, including taper and measurement-delay effects, unless a separate
correctly targeted idle-analysis binding is designed and qualified. Enabling
Idle VE support does not by itself enable automatic tuning of that table.

Acceptance must cover both target models crossed with both axis choices,
independent cursor/source routing, an inactive target at either endpoint,
required-input faults, entry/exit and taper transitions, warm-up/cranking-taper
interaction, endpoints and mixed authority, no duplicate corrections, and
standalone migration. Include changing idle-bypass airflow at fixed TPS in
engine qualification: an Idle VE table alone does not prove that airflow is
accounted for. Keep table dimensions unchanged; resizing remains excluded.

## 8. Selectable air temperature, shared by both blended models

The owner requires a user-selectable temperature source for SD and Alpha-N:

| Selection | Temperature used in the air-mass equation |
| --- | --- |
| Tcharge | Estimated charge temperature from the configured charge-temperature model. |
| IAT | Measured intake-air temperature from the configured IAT sensor. This is the temperature at the sensor location, not necessarily outside ambient temperature. |

In standalone operation the selection applies to the active SD or Alpha-N
model. **In blended operation a single selection applies to both models.**
Capture and resolve the selected temperature once for each composite
calculation. Supply the same value, Kelvin conversion and validity result to
the SD and Alpha-N branches, including at 0% and 100% authority and while
Idle VE contributes. Do not retain an independent `alphaNUseIat` switch that
can silently override the shared selection in the composite path.

Keep this temperature source separate from each consumer's load-axis choice,
Idle VE model ownership and authority. Selecting a temperature source does not
change the pressure reference, enable MAP multiplication or change the meaning
of a model when entering blended operation. MAF's measured mass must not receive
an additional ideal-gas temperature correction through this setting.

For Tcharge, retain the configured estimation method and rate limits; airflow
methods must continue to use their documented previous-calculation input, not
create a circular dependency on the mass currently being calculated. Define
the estimator's input and fallback validity contract explicitly. For IAT,
require a valid measured temperature. Reject an invalid selected result under
the applicable fault policy; do not silently switch sources. Inputs can remain
required by other active functions even when this temperature choice does not
use them.

Display and log the selected source and actual temperature used. Review the
existing IAT fuel correction, warm-up enrichment and other thermal corrections
to distinguish residual/fuel effects from air-density compensation. Apply each
effect at its defined stage once; do not automatically zero unrelated correction
tables when changing this selector.

Changing IAT to Tcharge can change calculated mass with identical VE cells.
The setup procedure must explicitly review the main/idle calibrations and
thermal corrections after a change. The pre-revision composite used Tcharge for
SD and IAT or fixed 20 C for Alpha-N; one shared source cannot generally reproduce
that earlier combination exactly. Likewise, the old fixed-20-C Alpha-N setting
has no direct equivalent among these two source choices. Document these cases
instead of claiming that a field rename or copying cells preserves behavior.

Acceptance must cover both choices in standalone and composite operation,
the same captured temperature reaching both contributing branches, endpoint
parity under matched inputs and pressure policy, intermediate authority, Idle VE, missing/invalid
inputs, consistent Kelvin conversion, Tcharge rate limits and thermal changes.
Include equal-temperature cases and differing IAT/CLT cases to expose accidental
use of the old branch-specific source. The revision adds numerical regression
coverage; thermal behavior still needs separate engine qualification.

## 9. Optional MAP multiplication for standalone Alpha-N

The owner requires a user-selectable `Multiply MAP` option for **standalone
Alpha-N only**. The main Alpha-N table remains indexed by TPS and RPM in both
settings. This option changes the mass equation, not the lookup coordinate.

| Operating mode | Stored Multiply MAP setting | Base Alpha-N pressure term, before optional barometric compensation |
| --- | --- | --- |
| Standalone Alpha-N | Off | Fixed reference pressure, 101.325 kPa, as in the existing Alpha-N model. |
| Standalone Alpha-N | On | Effective MAP, producing the hybrid Alpha-N calculation. |
| SD + Alpha-N composite, including 100% Alpha-N authority | Either | Fixed reference pressure, 101.325 kPa; the standalone option has no effect. |

At identical table value and selected temperature, define the hybrid calculation
as `mass_hybrid = mass_pure_base * effective_MAP / 101.325 kPa`, where
`mass_pure_base` excludes the optional barometric compensation in section 10, or equivalently
substitute effective MAP for the reference pressure in the ideal-gas equation.
Do not multiply by an unnormalised pressure or apply the pressure term twice.
Keep the temperature choice from section 8 independent of this option.

Use the established effective-MAP resolution, including valid estimation and
transient handling when permitted by `Use MAP estimate table` and its subordinate
transient setting. Expose the applicable controls in hybrid standalone Alpha-N.
A required pressure that cannot be resolved must follow the applicable input
fault policy; do not silently revert to pure Alpha-N. With Multiply MAP off,
this calculation does not require MAP, although another active function may.

Default the option to off to preserve the existing Alpha-N pressure equation.
In composite mode show it as inactive and identify the effective model as pure
Alpha-N, regardless of the stored standalone choice. Retain the standalone
setting across mode changes, but make clear which equation will be used when
returning to standalone operation. Do not let global mode checks hidden inside
a generic lookup change table ownership: pass the pressure policy explicitly
to the evaluation path, including diagnostic/dry queries, without temporarily
modifying global configuration.

The same Alpha-N map storage serves both choices. A map calibrated for hybrid
standalone operation is not automatically calibrated for pure composite
operation. Document the need to prepare the appropriate main and Idle VE
calibrations before changing pressure policy or entering composite mode from
hybrid standalone operation. Qualify pure Alpha-N for blending at 100% Alpha-N
authority, or in standalone with Multiply MAP off and matching temperature,
correction and downstream-load settings. Do not promise endpoint equivalence
between hybrid standalone and pure composite operation.

Hybrid Alpha-N with Tcharge can reproduce the mass-equation structure of legacy
SD with a TPS-indexed VE table. Reproducing the complete prior behavior also
requires matching effective pressure, axes, correction inputs and each
downstream consumer's source; changing the strategy alone is insufficient.

Acceptance must cover on/off standalone behavior with both temperature choices,
the pressure-ratio relationship, Idle VE, required MAP and estimate faults,
mode transitions, and composite results unchanged by the stored standalone
option at 0%, intermediate and 100% Alpha-N authority. SD and MAF behavior must
remain unaffected by the standalone option. Section 10 defines the separate
barometric compensation for pure Alpha-N; neither option removes the existing
downstream barometric correction. Airbox compensation requires separate
qualification.

## 10. Barometric compensation for pure Alpha-N

The accepted addition is implemented: pure Alpha-N can follow ambient-pressure
changes while retaining TPS and RPM as the main map axes. It defaults off and
still requires calibration and qualification for the intended engine.

### Controls and applicability

- `Use barometric compensation for Alpha-N`.
- `Barometric reference pressure`, in kPa: the pressure to which the Alpha-N
  calibration is referenced.

Default compensation to off so existing calibrations are not implicitly changed.

| Operating mode | New automatic density compensation |
| --- | --- |
| Standalone Alpha-N, Multiply MAP off | Applicable when enabled. |
| Standalone Alpha-N, Multiply MAP on | Inactive; MAP already supplies the pressure term. |
| SD + Alpha-N composite | Applicable to the Alpha-N branch only, before blending. |
| SD or MAF | No new automatic BARO density multiplier. |

The implemented formulation, subject to engine qualification, is:

```text
alphaNMassCorrected = alphaNMassBase * ambientPressure / alphaNBaroReferencePressure
blendedMass = (1 - weight) * sdMass + weight * alphaNMassCorrected
```

Both pressures use the same absolute units. The selected Tcharge/IAT source
remains independent and is shared by the two models in blended operation.
This pressure ratio is a physical approximation to validate on the engine,
not a guarantee that every altitude effect is linear.

### Calibration reference and existing corrections

A new calibration can use 101.325 kPa as its reference. For an existing map,
using the ambient pressure at which it was calibrated can preserve the result
at that operating condition, provided no equivalent compensation was already
applied. The reference must be explicit and stable; do not reset it at each
power-on. Distinguish this calibration reference from the measured or sampled
ambient pressure used in the numerator. Validate that the configured reference
is finite and positive.

Retain the existing common barometric table for residual correction. Review its
calibration to prevent a second application of the Alpha-N density effect; do
not blindly multiply both branches by the new ratio or silently rewrite the
existing common table. Make the common correction accessible in all modes where
it affects running fuel, with its scope described explicitly.

Publish the corrected Alpha-N contribution and use it for final blended mass
and mass-derived quantities. Apply the compensation also when the assigned
Alpha-N Idle VE contributes. Preserve the existing placement of other shared
corrections and apply each effect once.

### Inputs, diagnostics and qualification

Prefer continuous BARO measurement when altitude changes during use. Distinguish
continuous measurement, pressure captured at startup, and invalid input in
diagnostics. Capture the pressure consistently for the calculation and report
the applied coefficient. Define the response to missing or invalid BARO
explicitly; do not silently substitute 101.325 kPa. Compensation disabled, an
inapplicable mode, or a zero-contribution Alpha-N branch must not introduce a
BARO dependency through this feature. Other active functions can still need it.

Qualification must cover disabled behavior, coefficient 1 at the reference,
pressure changes, both temperature sources, authority endpoints and mixed
weights, Idle VE, invalid input, and interaction with the common barometric
table. Verify that enabling the standalone MAP multiplier suppresses this
automatic BARO factor, while the stored MAP option cannot suppress it in
composite mode. Compare behavior across ambient conditions on the engine.

Airbox pressure can differ from outside ambient pressure through restriction
or dynamic pressure. Using it instead requires explicit source selection and
separate qualification; do not silently reinterpret a BARO sensor as an airbox
sensor. Boosted applications are not implicitly qualified by this proposal.

## 11. Study of barometric normalisation of MAP estimate

The owner accepted documenting this as a separate study. It is **not implemented
and is not a qualified implementation requirement**. The derivation, assumptions
and proposed validation are in the [MAP-estimate BARO study](map-estimate-baro-study.md).

### Problem and hypothesis

The MAP-estimate table currently stores absolute kPa values indexed by TPS and
RPM. A table calibrated at one ambient pressure may become less representative
after an altitude change, including when used with a functioning MAP sensor
for transient comparison.

Evaluate the following approximation:

```text
correctedMapEstimate = mapEstimateTableValue * ambientPressure / mapEstimateBaroReferencePressure
```

This assumes approximately constant MAP/BARO at the same TPS and RPM. Validate
the assumption on the engine, especially at idle and with changing bypass air.
The estimate table's calibration reference is its own parameter and need not
equal the Alpha-N calibration reference. It must not be reset at each startup.

### Conditional integration

Only if qualified, introduce:

- enablement independent of Alpha-N barometric compensation, subordinate to
  permission to use the MAP-estimate table;
- an explicit reference pressure for the MAP-estimate calibration;
- correction of the estimate before fallback use or transient comparison with
  measured MAP, without modifying measured MAP itself;
- diagnostics for the original estimate, corrected estimate and coefficient;
- an explicit required-input and failure contract while the correction is used.

Every consumer of the same estimate must receive the same corrected value,
including SD and hybrid standalone Alpha-N. Do not add another mass correction
after their pressure-based calculation to account for this same normalisation.

Compare estimates against measured MAP at multiple ambient pressures, including
idle, changing bypass opening, partial/full throttle and transients. Verify the
measured-versus-estimated selection with the corrected estimate. Keep the current
behavior until the study supports adoption; do not extend the approximation
implicitly to boosted engines. Table dimensions remain unchanged.

## Migration and operating procedures

- Convert old shared-map calibrations into the map owned by their selected
  model. Preserve independently calibrated maps in already-dedicated tunes.
- Detect old VE-axis overrides that cannot be represented by the target model's
  fixed natural axis. Report the incompatibility and required recalibration;
  do not silently reinterpret an existing table as another physical model.
- Initialize each new consumer selector to its previously effective source.
  Preserve old standalone TPS/filling behavior and old composite MAP behavior,
  including consumers that previously inherited another selector.
- Define version/signature changes and the manual TunerStudio backup/restoration procedure explicitly. Automated conversion scripts are excluded.
  Preserve applicable standalone idle-map and correction behavior. Migrate
  Idle VE enablement, load-axis choice, calibration and model ownership
  explicitly when introducing its composite support.
- Configure and document the temperature source explicitly, including the
  cases in section 8 where the previous thermal behavior cannot be preserved
  by the two-choice selector. Do not silently promise numerical equivalence.
- Record the standalone Alpha-N Multiply MAP choice and its effective state
  in each mode. Document recalibration when moving between hybrid standalone
  and pure blended operation, including the assigned Idle VE table.
- Configure Alpha-N barometric compensation and its calibration reference
  deliberately. Review pre-existing common barometric corrections and document
  the source and validity of ambient pressure. MAP-estimate normalisation remains
  a separate study and is not enabled by this setup step.
- Update the Italian operator guide, tooltips and release/PR descriptions with
  all new or removed controls, editor visibility, dependencies and examples.
  Show how to reproduce previous operation, enable blending, choose different
  loads for different consumers, and calibrate at qualified endpoints.

## Development and acceptance

1. Inventory load consumers and migration cases, including the confirmed
   MAP-estimate permission and its subordinate transient setting.
2. Implement default dedicated maps, mode selection, naming and readiness removal
   together with manual restoration instructions and numerical regression coverage.
3. Implement independent consumer selectors, source resolution, units, cursors
   and dependency checks, including Idle VE support with the agreed model
   ownership, the temperature source shared by both blended models, and
   standalone-only Alpha-N MAP multiplication. Add optional pure-Alpha-N
   barometric compensation before blending, with explicit input dependencies
   and review of existing common corrections.
   Use mixed MAP/TPS selections to prove load-source independence, and both
   temperature selections to prove common temperature resolution.
4. Qualify endpoint analyzer bindings and filtering; enable only supported cases.
5. Update operational documentation to match the implementation, run appropriate
   host regression and manual restoration checks, then build Core8 and Proteus F7. Measure new
   selectors and diagnostics within existing RAM, flash, stack and packet budgets.
6. Perform isolated bench checks of mode changes, sensor loss, recovery and
   TunerStudio edits. Engine calibration remains a separate qualification step.

Track the MAP-estimate barometric-normalisation study separately. Completing
this revision does not imply that the study's proposed correction is implemented
or qualified; any adoption requires its own recorded evidence.

Use Astra high for architecture, dependency/migration design and difficult
reviews; use 5.6 Sol high for bounded UI, documentation and test work. Keep
independently reviewable changes on the integration branch; publishing PRs is
separate from updating these project notes.

Acceptance requires unchanged supported standalone results where the prior
configuration is representable, and explicit qualification of changes otherwise.
It also requires three separately retained maps, no manual model-readiness gate, correct map
selection at 0%/100% and mixed authority, independent source routing for every
consumer, coherent UI units/cursors, explicit sensor-fault behavior, Idle VE
with its independently selected load axis and qualified transitions, a shared
selected temperature in both blended models, a standalone-only Alpha-N MAP
option that cannot change composite evaluation, optional barometric compensation
of the pure Alpha-N contribution without duplicated density correction, and analyzer writes restricted
to their qualified map and samples. Table dimensions remain unchanged
throughout this revision.

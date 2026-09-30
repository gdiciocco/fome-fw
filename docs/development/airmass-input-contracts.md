# Captured air-model inputs and load coordinates

## Status

This describes the revised calculation on `feature/blended-airmass`, updated
2026-09-30. The [revision requirements](blended-airmass-revision.md) govern its
scope. Integration checks and hardware evidence belong in the
[validation record](blended-airmass-validation.md); an implemented equation is
not evidence of engine calibration quality.

All three models always use their own stored maps and axes. The dedicated-map
opt-in, main VE axis override and manual model-readiness declarations no longer
control evaluation. SD and Alpha-N can contribute to a mass blend; MAF remains
standalone. Map dimensions and encodings are unchanged.

## Acquisition and evaluation

`AirmassInputs` retains RPM, measured MAP, resolved effective MAP, TPS1, pedal,
IAT, the selected temperature and its validity, BARO and its provenance, idle
activity and driver intent, engine geometry, model identity, native load and
relevant selectors. Calibration tables stay in configuration storage.

Acquisition is sequential: this is a retained set of samples, not simultaneous
sampling of asynchronous sensors. Both blended branches receive the same input
object. They do not re-read temperature, pressure or TPS. Sensor acquisition
alone does not create an input dependency; a captured but unused input can be
invalid without failing the calculation.

Each model returns grams per cylinder, native load and explicit validity.
Optional diagnostics hold values, not references to later live state. Raw SD
and Alpha-N evaluation includes their applicable Idle VE overlay and, for pure
Alpha-N, optional BARO compensation. Raw evaluations exclude common VE
corrections and downstream fuel corrections.

### Shared temperature

| Selection | Captured value and validity |
| --- | --- |
| Tcharge | Existing rate-limited `engineState.sd.tChargeK`; must be finite and greater than zero. |
| IAT | Valid measured IAT, converted using `convertCelsiusToKelvin` (+273.15); Kelvin value must be finite and greater than zero. |

The selection applies to standalone SD or Alpha-N and to both branches of the
blend, including the authority endpoints and Idle VE. An invalid selected
result fails the model; the other source is not substituted. MAF's measured
mass does not receive this ideal-gas temperature correction.

Tcharge retains the estimator's existing input fallbacks: missing CLT uses IAT;
missing IAT contributes 0 C to the CLT interpolation; both missing produce 0 C;
an invalid coefficient or interpolation falls back to CLT. Its rate limits and
previous-calculation airflow input are unchanged. Thus a finite Tcharge is a
usable estimator result, not proof that both temperature sensors are healthy.
Other active corrections can independently require IAT or CLT.

`airmassTemperature` logs the actual selected value in C and
`airmassTemperatureSourceUsed` identifies the selection. Captured diagnostics
also retain validity. The old Alpha-N fixed-20-C option has no direct selectable
equivalent; old mixed SD/Tcharge and Alpha-N/IAT behavior cannot generally be
reproduced by one shared selection. Review main/idle calibration and thermal
fuel corrections when changing source.

### Pressure equations

For cylinder volume `V`, table fraction `v` and selected Kelvin temperature `T`,
the ideal-gas helper computes `m = v * V * P / (0.28705 * T)` in grams.

| Model/path | Pressure term |
| --- | --- |
| SD | Effective MAP. |
| Standalone Alpha-N, Multiply MAP off | 101.325 kPa, followed by optional pure-Alpha-N BARO compensation. |
| Standalone Alpha-N, Multiply MAP on | Effective MAP; automatic Alpha-N BARO compensation is inactive. |
| Blended Alpha-N, at every authority | 101.325 kPa, followed by optional pure-Alpha-N BARO compensation. |

The Alpha-N main map always uses TPS/RPM. Its pressure policy is an explicit
argument of the raw API and an available public dry-evaluation overload;
composite evaluation explicitly requests `PureReference`. It never mutates
configuration to suppress the retained standalone Multiply MAP setting.

When enabled and applicable, `alphaNMass *= BARO / alphaNBaroReferencePressure`.
The reference must be finite and positive. Captured BARO must be valid, finite,
positive and at most 200 kPa. Missing BARO is not replaced with standard
pressure. The corrected Alpha-N mass is published as its branch contribution
and enters the blend and final normalized filling. The factor also applies to
Alpha-N-owned Idle VE. Disabled compensation, hybrid standalone Alpha-N, SD,
MAF and a skipped Alpha-N branch add no BARO dependency through this option.

`alphaNBaroCoefficient` reports the applied factor (1 when inapplicable).
`airmassPressureFlags` uses bits 1 = valid continuous BARO, 2 = valid startup
BARO, 4 = invalid BARO, and 8 = standalone MAP multiplication active. Startup
BARO has the sensor layer's explicitly captured startup MAP provenance; it is
not continuous ambient measurement. The existing common barometric fuel table
remains a later residual correction and needs calibration review to avoid
repeating the density effect.

## MAP-estimate permission

`useMapEstimateTable` defaults off and is a permission to use the existing
TPS/RPM estimate, not a declaration of calibration accuracy.

| General permission | Subordinate transient setting | Resolution |
| --- | --- | --- |
| Off | Either stored value | Measured MAP only; no estimate lookup or comparison. |
| On | Off | Valid measured MAP, or a valid estimate when measured MAP is unavailable. |
| On | On, transient trigger active | Compare valid measured MAP and estimate; select the greater. If measured MAP is unavailable, use the valid estimate. |

An estimate requires valid bounded TPS, finite nonnegative RPM, valid axes and
table contents, and a finite result in 0..600 kPa. Measured/effective MAP must
be finite and in 0..1000 kPa. A requested transient comparison requires a valid
estimate even when measured MAP would win. Unused estimate contents do not
invalidate a healthy measured MAP.

The same resolver serves SD, hybrid standalone Alpha-N and independent
effective-MAP consumers. An estimate never supplies an explicitly selected
measured-MAP source. [MAP-estimate BARO normalisation](map-estimate-baro-study.md)
remains study only and is not applied by this resolver.

## Idle VE and corrections

The single Idle VE table has an independent load selector and an explicit
SD/Alpha-N owner in the blend. Standalone SD, Alpha-N and MAF retain ownership
of their own overlay regardless of the composite target selection. The four choices are model default (the owning model's native load), measured
MAP, TPS and effective MAP. The dedicated Idle selector does not expose final
cylinder filling, which would depend on the mass being calculated, or pedal.

The existing idle controller determines idle/taper activity, including its
configured cranking-to-idle taper behavior. Driver throttle intent determines
the return to the main table: up to half the deactivation threshold uses the
idle value fully; half to the full threshold interpolates to the main value;
at the full threshold the idle table no longer contributes. The selected load
axis changes the lookup only. It does not change idle detection or authority.

Only a contributing target branch evaluates its overlay. At zero overlay
weight its axes and input are unused. When it contributes, its source, axes,
threshold, driver intent and table result must be valid. Idle RPM bins retain
their uint8 × 10 RPM encoding (storage maximum 2550 RPM, editor limit 2500 RPM).
Changing ownership or axis units requires deliberate recalibration.

Each standalone model applies common VE corrections after its idle/main
interpolation. Composite operation blends the two raw masses, then applies
common VE corrections once. The correction pass uses captured core channels;
other configured GPPWM channels are sampled once per distinct channel. Missing
required correction inputs invalidate the result. Default correction load is
the standalone native load or composite effective MAP. GPPWM fuel/ignition-load
channels retain the previous published calculation, avoiding a circular lookup
through the current corrected mass.

IAT fuel correction, warm-up, common BARO correction and other downstream fuel
effects keep their separate stages. Changing air-model options does not rewrite
or neutralize those calibrations.

## Independent load contract

| Selector | Source | Units |
| --- | --- | --- |
| Default | Native load of the standalone model; effective MAP in the blend. | Model-dependent. |
| MAP | Captured measured MAP. | kPa. |
| TPS | Captured TPS1. | Percent. |
| Accelerator pedal | Captured pedal. | Percent. |
| Cylinder filling | Final mass / standard cylinder charge. | Percent. |
| Effective MAP | Captured effective MAP, including an accepted estimate. | kPa. |

Standalone SD Default retains effective-MAP validity and estimate provenance;
standalone Alpha-N Default requires valid captured TPS. MAF Default is its
uncorrected native filling, while the explicit cylinder-filling selection uses
corrected final mass. A numeric zero native load alone cannot establish a
valid SD or Alpha-N source.

Independent selectors are resolved from the same calculation context through
`processAirmassConsumerLoads`. Required active consumers must all resolve before
the live owner accepts a fuel calculation. Dry queries validate without
publishing. The live owner retains a compact source/validity snapshot for later
lookups; it does not silently switch a configured source after sensor loss.
The consumer inventory documents each table/control's activation and units.
Legacy diagnostic pressure/filling outputs retain their physical meanings;
they do not choose a table's calibration units.

## Integration and verification boundary

`AirmassEvaluation.Valid` must reach the live fuel owner, including standalone
SD, Alpha-N and MAF; zero numeric mass cannot encode all faults. SD/Alpha-N
`getAirmassForFuel` and the MAF equivalent publish diagnostics while retaining
validity. Composite readiness additionally requires complete per-cylinder fuel
publication and its established latched fault/recovery contract. Unused endpoint
branches do not run. No surviving model is automatically promoted after failure.

Host tests cover captured provenance, input validity, shared-temperature
conversion, explicit hybrid pressure ratios, conditional BARO, independent
Idle VE ownership/axes, exact endpoints, correction placement and MAF behavior.
Current run counts and board evidence are recorded separately during
integration. The previous 2026-09-29 baseline passed 747 tests; that baseline
does not validate this revision's new equations, selectors or fault paths.
Bench checks can verify software and scheduling behavior; engine thermal,
altitude and changing idle-bypass behavior still require qualification.

# Blended airmass load consumer inventory

Initial audit: 2026-09-29. Implementation status: 2026-09-30, final candidate `88f07ff8f0`.
The tables and audit sections retain the pre-revision source inventory alongside
the accepted selector boundaries, which are now implemented. Wording such as
“currently” in an audit description refers to that historical baseline; the
current-status sections below describe the resulting implementation. Qualification
status and representative regressions are in the
[revision validation record](blended-airmass-revision-validation.md). Scope follows
[revision section 5](blended-airmass-revision.md#5-independent-load-selection-for-every-consumer).

All paths below are relative to `firmware/`. Generated headers/INI files were
excluded from the implementation search. Searches followed `fuelingLoad`,
`ignitionLoad`, their getters, `EngineLoadPercent`, `afrTableYAxis`,
`getResolvedLambdaLoad`, `readGppwmChannel`, and `calculateBlend`.

## Current implementation status

Direct consumers resolve their own selectors through `processAirmassConsumerLoads`
and the captured `AirmassLoadSnapshot`; later callbacks use
`getAirmassConsumerLoad`/`getAirmassSelectedLoad`. Runtime coordinates retain full
precision independently of packed cursors. Configuration/version changes,
strategy changes, stops and faults invalidate stale publication.

The required-input union follows active consumers. Disabled ignition skips timing
lookups, while the torque controller still updates. Disabling injection does not
remove a target-lambda dependency when STFT or lambda protection actually uses
that target. `isAirmassLambdaTargetRequired()` exposes the captured requirement;
when no consumer needs it, fuel conversion is skipped and unused fuel/target
publications are cleared. A remaining active GPPWM load alias can still require
the source associated with disabled ignition or injection.

Fan AC-on tables and each boost correction X axis now have independent selectors.
Idle VE has a dedicated four-choice enum: model default, measured MAP, TPS and
effective MAP. Its owner is independently SD or Alpha-N in composite mode; it
cannot select final cylinder filling and create a calculation cycle.

The audit's manual-restoration requirements remain relevant: selector defaults do
not convert existing calibration cells or units. No table was resized. Host tests
and generated-layout checks pass, as do both ARM builds and the complete isolated
Core8 bench (55 samples). Interactive editor checks passed and the original flash
and configuration were restored byte-for-byte. The validation record defines the
GUI scope; running-engine calibration is separate.

## Source semantics and API

In the audited blended implementation, `fuelingLoad` is effective MAP in kPa,
even at 100% Alpha-N authority. `ignitionLoad` follows `ignOverrideMode`.
`EngineLoadPercent` is therefore a historical name, not a unit guarantee.
`normalizedCylinderFilling` is the physical air mass divided by standard cylinder
air charge, in percent. These values are not interchangeable.

`load_override_e` retains values 0–4: None/legacy default, measured MAP, TPS,
accelerator pedal and normalized cylinder filling. Effective MAP is appended as
value 5; measured MAP retains its meaning. `GPPWM_EffectiveMap` is appended as
value 35 without renumbering existing choices. Boost retains its TPS-channel
mapping to driver throttle intent, distinct from the general GPPWM reader.

Implemented API contracts:

- Resolve a consumer's selected source to a value plus validity, source, units,
  and estimate provenance. Reuse `AirmassLoad`/`resolveAirmassLoad` where possible.
- Keep legacy-default behavior an explicit caller argument or migration result;
  do not let a new consumer inherit another consumer's live selector.
- Resolve from the captured calculation inputs during fuel calculation. Publish
  a coherent, validated snapshot for later module callbacks and diagnostics.
- A missing selected TPS/MAP/pedal source is invalid. Never replace it with a
  different physical coordinate. Legacy substitution behavior outside blended
  operation requires explicit preservation or migration.
- Use a typed consumer ID plus index, or named fields/arrays. One global selector
  is insufficient; per-cylinder independently calibrated tables need per-cylinder
  selectors even though their numeric axis arrays are presently shared.
- The input-dependency pass must use the same active-consumer enumeration and
  source resolver as execution, to avoid divergent sensor requirements.

`load_override_e` uses three bits and `gppwm_channel_e` uses six bits, including
the added choices. No map sizes or encodings changed. New selector storage changes
persistent layout and requires explicit manual restoration.

## Direct consumers and closely related lookups

“Fuel default” below means the historical strategy load (effective MAP in the
blended mode); “ignition default” means the historical resolved ignition load.
Existing tune migration should copy the source that actually fed each table,
not merely install the new-tune defaults.

| Consumer / implementation | Pre-revision source and selector | Implemented selector and restoration source | Editor, thresholds, and validity |
| --- | --- | --- | --- |
| Lambda target: `controllers/algo/fuel/fuel_computer.cpp`, `getTargetLambda`; blend resolution in `airmass/blended_airmass.cpp` | `afrOverrideMode`; blended `LambdaLoad`; standalone `getTargetLambdaLoadAxis` | Reuse `afrOverrideMode`, add explicit effective MAP. Preserve all existing choices. | Lambda and AFR are two views of **one** calibration, so one selector is appropriate. Both cursors use resolved lambda load. Validate positive finite target and stoich before packed publication. |
| Main ignition: `controllers/algo/ignition/ignition_state.cpp`, `getRunningAdvance` | `ignitionLoad` from `ignOverrideMode` | Reuse `ignOverrideMode`; preserve existing options. | `ignitionTable`, `ignitionLoadBins`; retain cranking transition use of running table. Fixed timing bypasses the lookup. |
| Injection phase: `controllers/algo/engine2.cpp` → `fuel_math.cpp::getInjectionOffset` | Fuel default, no selector | `injectionPhaseLoadSource`; fuel default | `injectionPhase`, `injPhaseLoadBins`; cursor currently `fuelingLoad`. Invalid load must reject calculation before injection publication. Existing NaN checks alone do not establish sensor validity. |
| Cylinder fuel trim 1–12: `engine2.cpp` → `fuel_math.cpp::getCylinderFuelTrim` | One prepared interpolation from fuel default shared by all cylinders | `fuelTrimLoadSource[MAX_CYLINDER_COUNT]`; each migrated to fuel default | Each `fuelTrims[i].table` must resolve its own coordinate. Shared `fuelTrimLoadBins` may retain numeric values but each editor must show its own units/cursor. Prepare per distinct coordinate or per cylinder. Validate each active cylinder's resulting mass. |
| Cylinder ignition trim 1–12: `engine2.cpp` → `ignition_state.cpp::getCylinderIgnitionTrim` | One prepared interpolation from ignition default shared by all cylinders | `ignitionTrimLoadSource[MAX_CYLINDER_COUNT]`; copy prior main-ignition source to every entry | Each `ignTrims[i].table` gets its own coordinate and cursor despite shared `ignTrimLoadBins`. Changing main ignition selector later must not change these selectors. |
| STFT region selection: `controllers/math/closed_loop_fuel.cpp::fuelClosedLoopCorrection/computeStftBin` | `getFuelingLoad()`, no selector | `stftLoadSource`; fuel default | One region-classification control uses `maxOverrunLoad` and `minPowerLoad`; both thresholds and the hardcoded 2-unit hysteresis use this source's units. Idle region is RPM based. Evaluate only when correction is enabled/eligible; invalid selection must not silently pick a cell. Per-bank cells currently share region boundaries, so this is one selector for the existing classifier. |
| Injector staging: `engine2.cpp` → `fuel_math.cpp::getStage2InjectionFraction` | Blended `getResolvedLambdaLoad()`, standalone packed `afrTableYAxis`; inherits lambda choice | `stagingLoadSource`; migrate by copying old `afrOverrideMode` resolution | `injectorStagingTable`/load bins; independent cursor. Existing 10%/3% hysteresis acts on table **output fraction**, not input load. Only active when staged injection enabled. |
| Lambda deviation table: `controllers/math/lambda_monitor.cpp::getMaxAllowedLambda` | Receives fuel default from `engine2`, despite older cursor using AFR load outside blended | `lambdaDeviationLoadSource`; fuel default | `lambdaMaxDeviationTable`/load bins needs its own cursor. Its output remains a lambda deviation added to actual target lambda; selecting load does not change target source. |
| Lambda monitor activation/restoration control: `lambda_monitor.cpp::isCurrentlyGood/restoreConditionsMet` | Same argument as deviation table | `lambdaMonitorLoadSource`; fuel default; independently selectable from the table | `lambdaProtectionMinLoad` and `lambdaProtectionRestoreLoad` are this control's thresholds. Min/restore TPS remain explicitly TPS. Do not let selecting the table coordinate also change the control thresholds. Invalid load must not masquerade as low-load good lambda or restore a cut. |
| Trailing spark: `engine2.cpp`, interpolation after per-cylinder publication | **Fuel default at runtime**, while editor shows `ignitionLoad` | `trailingSparkLoadSource`; fuel default | `trailingIgnitionTable`/load bins; correct the longstanding runtime/editor mismatch. Do not migrate using the old editor's implied source. |
| IAT ignition correction: `ignition_state.cpp::updateAdvanceCorrections` | Main ignition default, no independent selector | `ignitionIatLoadSource`; copy prior main-ignition source | `ignitionIatCorrTable`/load bins, temperature X axis. IAT failure currently gives zero correction. CLT correction is a temperature-only curve and has no blended-load input. |
| Knock maximum retard: `controllers/engine_cycle/knock_controller.cpp::getMaximumRetard` | `getIgnitionLoad()` | `knockRetardLoadSource`; copy prior main-ignition source | `maxKnockRetardTable`/load bins. Output remains degrees. Knock base-noise threshold is RPM-only, so it needs no load selector. |
| Cylinder knock gain 1–12: `knock_controller.cpp::onFastCallback` | `getIgnitionLoad()` shared for every `knockGains[i].table` | `knockGainLoadSource[MAX_CYLINDER_COUNT]`; each copies prior ignition source | Each active cylinder needs an independent selector/cursor; shared `knockGainLoadBins` remain numeric axes. Knock runs after EngineState fuel/ignition calculation in module callbacks. |
| Intake VVT target: `controllers/actuators/vvt.cpp::getSetpoint` | Fuel default unless `vvtIntakeYAxisOverride != GPPWM_Zero` | Reuse existing independent GPPWM selector and add effective MAP | `vvtTable1`; actual cursor `VVT_B1I_targetYAxis`. Missing override currently becomes zero via `value_or(0)`, which must not hide a failed selected source in strict blended validation. |
| Exhaust VVT target: same function | Fuel default unless `vvtExhaustYAxisOverride != GPPWM_Zero` | Reuse exhaust selector independently of intake | `vvtTable2`; cursor `VVT_B1E_targetYAxis`. Banks share the corresponding target calibration, not independent tables. Other VVT enable logic uses RPM, CLT, elapsed time, and target-angle hysteresis; no additional hidden load threshold was found. |
| HPFP target pressure: `controllers/engine_cycle/high_pressure_fuel_pump.cpp::HpfpQuantity::calcPI` | Hardcoded measured MAP (`Sensor::getOrZero(Map)`); no selector | `hpfpTargetLoadSource`, default/migration **measured MAP** | `hpfpTarget`/load bins needs actual selected cursor. Output, rail-pressure feedback, PI error, target-decay rate remain kPa. Active HPFP should require the selected coordinate, not unconditionally measured MAP. |
| Idle VE: `controllers/algo/airmass/airmass.cpp::evaluateVe` | `idleVeOverrideMode`: inherited model axis, measured MAP, TPS; currently rejected in blended | Preserve old options; add explicit effective MAP and a distinct SD/Alpha-N owner in blended | `idleVeTable`/load bins; publish actual Idle VE cursor. Apply inside the owning contributing branch before mass blend. Owner with zero authority must not require idle-only sensors. TPS idle/taper return logic retains its established role. |

## Indirect GPPWM consumers

Every selector below already accepts TPS and other channels. Add effective MAP
to the shared channel enum/reader and preserve all existing channel choices.
Do not reinterpret existing `GPPWM_Map` as effective MAP. `GPPWM_FuelLoad` and
`GPPWM_IgnLoad` may remain compatibility aliases, but explicit choices must allow
the table to stop following the main ignition selector.

| Consumer / implementation | Selector granularity and audited default | Cursor and dependencies |
| --- | --- | --- |
| Four VE corrections: `airmass/airmass.cpp::evaluateAirmassCorrectionsImpl`, standalone `evaluateVe` | Each `veBlends[i].yAxisOverride` independently selects the table coordinate; `blendParameter` independently selects the bias-curve input. Zero override inherits effective MAP in blended, native model coordinate standalone. Zero parameter disables correction. | `veBlendYAxis[i]`, `veBlendParameter[i]`. Both parameter and table coordinate enter the dependency union when enabled. Fuel/IgnLoad parameters deliberately read the **previous** publication. |
| Four ignition corrections: `ignition/ignition_state.cpp::getRunningAdvance` → `math/engine_math.cpp::calculateBlend` | Each `ignBlends[i].yAxisOverride` and `blendParameter`; zero override inherits main ignition load | `ignBlendYAxis[i]`, `ignBlendParameter[i]`. Main ignition override must not forcibly select these axes. Current generic calculation silently disables on invalid parameter and substitutes zero for invalid override; strict blended requirements need explicit validation. |
| GPPWM channels 1–4: `actuators/gppwm/gppwm_channel.cpp::getOutput`; initialized in `gppwm.cpp` | Per channel `gppwm[i].rpmAxis` and `.loadAxis`, both may select either load alias | `gppwmXAxis/YAxis[i]`; output remains duty %. Only configured/active channels require inputs. Existing missing-input behavior is `dutyIfError`; preserve deliberate fallback behavior without changing the chosen coordinate. Output threshold hysteresis acts on duty, not selected load. |
| Open-loop boost table: `actuators/boost_control.cpp::getOpenLoop` | Own `boostOpenLoopXAxis/YAxis` | `Boost_boostOpenLoopXAxis/YAxisValue`. Preserve TPS-to-driver-intent compatibility. Output remains duty %. |
| Closed-loop boost target: `boost_control.cpp::getSetpoint` | Own `boostClosedLoopXAxis/YAxis` | `Boost_boostClosedLoopXAxis/YAxisValue`. Closed-loop-off bypasses this lookup. Target and plant observation remain pressure; pressure feedback cannot become TPS. |
| Two open-loop and two closed-loop boost correction tables | Each `boostOpenLoopBlends[i]` / `boostClosedLoopBlends[i]` has independent `yAxisOverride` and `blendParameter`. X currently inherits the parent boost X selector; since that selector can itself be load, fully independent load lookup requires a per-correction X override as well. | Existing per-correction published Y axis and parameter; add actual X cursor if separated. Default axes inherit the parent boost table; preserve old sources at migration. Bias-curve input is a separate selector. |
| Fan 1 AC-off, Fan 1 AC-on, Fan 2 AC-off, Fan 2 AC-on: `controllers/modules/fan/fan_control.cpp::getAcOffDuty/getAcOnDuty` | Currently two selectors `fan1PwmXAxis`, `fan2PwmXAxis`, each shared by two independently calibrated tables. Add independent AC-on selectors; migrate by copying each fan's old selector. | Existing shared `Fan_n_fanXAxisValue` is insufficient to present two differently selected table cursors. CLT remains the other axis. Missing input invokes `fanPwmSafetyDuty`. Shared numeric X bins can remain, with per-editor units. |
| Generic torque limiters 1–4: `controllers/algo/torque/torque_model.cpp::getTorqueLimit`, `evalGenericLimit` | `torqueLimiters[i].xAxis/yAxis` already independent per table; enabled flag gates evaluation | `m_limiterXAxisValue/YAxisValue[i]`. Outputs remain Nm; axle-to-engine conversion stays physical. Existing invalid input substitutes zero; dependency validation must see enabled limiters. |
| Torque-loss table: `torque_model.cpp::getTorqueLoss` | `torqueModel.torqueLossLoadAxis`, default CLT | `m_torqueLossLoadAxisValue`; output remains Nm. It can indirectly read either load alias. |
| Traction slip target: `controllers/algo/torque/traction_control.cpp` | `tractionControl.slipTargetYAxis`, default zero; the table's other input is speed | `slipTargetYAxisValue`. Preserve explicit configurable channel; output remains slip target. |

GPPWM sources can refer to GPPWM outputs, which may themselves depend on load.
Treat these as sampled derived outputs; do not recursively evaluate channels or
introduce algebraic loops. The current API exposes finite derived values without
proof of underlying sensor health. Document this limitation rather than claiming
that a finite output establishes all upstream inputs were valid.

## Physical outputs and non-consumers

- `controllers/can/obd2.cpp`, PID 0x04 currently transmits `getFuelingLoad()` as a
  percent. In blended this sends pressure through a percent contract. Use a
  physically defined, bounded percent publication for blended operation; the
  existing normalized filling is the available candidate. Do not make OBD load
  follow a tunable table selector. Exact calculated-load semantics should be
  documented separately from absolute normalized filling.
- OBD MAP PID 0x0B remains pressure; its present measured-MAP source is explicit.
- HPFP compensation is indexed by actual requested **cc per pump lobe** from
  per-cylinder injected mass. This is not an alias of blended load and must retain
  its physical quantity. Pump-volume, lobe-profile, deadtime, and rail-pressure
  feedback inputs also retain their units. The upstream target-pressure **table**
  still needs the independent load selector listed above.
- Torque-model air-mass inputs, airflow estimates, and normalized filling remain
  physical results of the mass calculation. Selecting a table coordinate must
  not rewrite them.
- Lua script tables have editor cursors tied to `fuelingLoad`, but firmware
  `lua_hooks.cpp`'s registered `table3d` function receives explicit `x` and `y` script arguments and
  does not implicitly consume the blended load. Preserve caller-owned lookup
  coordinates. Generic fuel-load editor cursors cannot claim to represent the
  actual script argument; either expose caller diagnostics or remove the
  misleading cursor. Do not insert firmware axis overrides into Lua's API.
- SD VE, Alpha-N reference filling, MAF correction, and authority maps keep their
  model-defined axes. Independent downstream selectors do not alter them.
- CLT fuel/timing curves, cranking enrichment curves, dwell, knock base noise,
  idle advance, ALS throttle-intent tables, TPS acceleration, and physical MAP
  controls do not read the blended-load aliases. Their names sometimes contain
  “load,” but that alone does not make them a blended-load consumer.

## Ordering, activation, and dependency hazards

1. `controllers/algo/engine.cpp::Engine::periodicFastCallback` calls
   `engineState.periodicFastCallback()` before module `onFastCallback()` methods.
   VVT, knock, and HPFP therefore consume the newly published state. GPPWM has its
   own update callback and consumes the latest publication.
2. `engine2.cpp` captures a calculation token, evaluates airmass/base fuel, then
   computes STFT, staging, phase, monitoring, ignition corrections/tables/trims,
   and cylinder masses. Fuel publication is protected against stale calculations.
   Resolve new selected coordinates before `completeCalculation`, and include
   invalid required coordinates in rejection. Trailing spark currently computes
   after that completion; account for its dependency before declaring readiness.
3. `SpeedDensityAirmass::captureInputs` captures previous fueling/ignition load
   before the new result is published. VE corrections use that capture to avoid
   a cycle through corrected mass. An explicit effective-MAP channel in this
   correction pass should read the captured effective MAP, not an old packed
   diagnostic or a later mutable global value.
4. Normalized-filling selections for downstream consumers need final corrected
   mass. Do not resolve them before correction. A pre-mass Idle VE lookup cannot
   depend on the final mass it helps calculate. Preserve only its established
   model-axis/MAP/TPS choices plus explicit effective MAP unless a separately
   specified prior-cycle source is introduced.
5. Build input requirements from contributing models, authority, active Idle VE,
   enabled corrections, and enabled downstream consumers. TPS selected for one
   table removes only that table's MAP requirement. An active SD contribution or
   any other MAP consumer may still require MAP. At pure Alpha-N authority with
   every active consumer on TPS, unconditional effective-MAP validation is wrong.
6. The old blended implementation separately hardcodes measured MAP as an HPFP
   dependency. Replace this with HPFP's selected source after adding the selector;
   keep rail-pressure feedback as an independent physical requirement.
7. Packed cursors are not inputs: `afrTableYAxis` saturates near 655.35 and VVT
   cursor values also have packed ranges. Resolve runtime loads at full precision
   and publish diagnostics separately. Never feed a saturated cursor back into a
   control or table.
8. Configuration writes, strategy changes, stops, and faults must invalidate the
   relevant published snapshot. A zero stale load must not authorize STFT,
   lambda-monitor restoration, ignition/knock interpolation, or HPFP target use.
   Existing deliberate fault latch and recovery semantics remain in force.

## Editor and migration acceptance criteria

- Every independently calibrated table/control above has its selector in
  `integration/fome_config.txt` and an exposed TunerStudio control, source-aware
  axis label/units, and truthful cursor in `tunerstudio.template.ini`.
- The same selected units label STFT and lambda-monitor thresholds. Numeric bins
  remain unchanged when selectors change; help text requires recalibration.
- New dependent selectors inherit the **old actual coordinate** during explicit
  migration: staging copies AFR, ignition trims/IAT/knock copy ignition, fuel trims/
  phase/STFT/trailing/monitor copy fuel, fan AC-on copies its old fan selector,
  HPFP remains measured MAP. Copy the resolved source, not a permanent link.
- Existing additional GPPWM/override options remain available. Existing alias
  choices may preserve source-following behavior as an explicit option, while
  effective MAP/TPS choices are independent.
- Per-cylinder selector arrays do not require new map storage or dimensions.
  They do require replacing shared prepared interpolation where loads differ.
- Shared physical outputs keep their original units. Lambda/AFR views of the
  same map and bank views of one VVT table are not separate calibrations.
- Validation tests should establish independence with deliberately distinct
  MAP/TPS values, sensor failure, inactive consumers, per-cylinder differences,
  estimated versus measured MAP, and configuration changes. The initial audit
  itself performed no builds or hardware operations; revision evidence is recorded
  separately in the linked validation record.

## Implemented standalone injection admission

The downstream revision includes injection admission for standalone SD,
Alpha-N, and MAF. Publishing zero fuel on an invalid calculation is insufficient:
the event scheduler applies wall-film correction and injector deadtime afterward.
`AirmassInjectionState` now requires a current, positive-RPM, valid final
publication before accepting a new scheduled pulse in those modes. The last
completed publication remains available during a new same-mode/version
positive-RPM calculation; its temporary working-validity reset does not interrupt
scheduling. A known input/result failure closes admission immediately, with a
token check preventing stale rejection of a newer result. Admission then stays
closed until the next valid publication; it does not create
a composite fault latch. Configuration writes, engine stops, and stale calculation
tokens also invalidate admission.

Already accepted output callbacks still complete, including their closing edges.
A failed calculation cannot enqueue a new split pulse. Standalone priming retains
its separate admission path. Lua and mock model identifiers retain legacy
admission behavior. Tests cover each standalone physical model, recovery without
rearming, old-token rejection, blocked wall-film processing, and accepted-pulse
completion.

VE Analyze session invalidation is cleared only by an engine-stop transition or
a configuration write with a confirmed physical stop. A zero or nonfinite RPM
sample suppresses qualification without starting a fresh session. Endpoint
qualification additionally requires uniform authority, no Idle VE, and the
startup delay; tuning software still applies its sample filters.

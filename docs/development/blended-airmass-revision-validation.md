# Blended airmass revision: implementation and validation

## Revision and evidence

Status recorded 2026-09-30 for source commit `88f07ff8f0`. The main revision is `d5459bbb58`;
`21b6718e5e` fixes standalone admission during recalculation and `2f2d0fed8f`
pads unsupported TunerStudio load-source codes with `INVALID`.
`88f07ff8f0` balances the PWM stop callback counter; the bench exposed its
accumulation after repeated simulator restarts. The preceding MLG header-offset
fix is `498e542e92`; it is a separate change.

This record covers the [accepted revision](blended-airmass-revision.md), sections
1–10. The [earlier validation record](blended-airmass-validation.md) describes the
pre-revision baseline. Its hardware results are historical evidence and do not
qualify this revision. Section 11 remains the separate
[MAP-estimate BARO study](map-estimate-baro-study.md), with no runtime adoption.

Integration artifacts are under
`/home/deffie/fome-artifacts/blended-airmass/revision-2026-09-30/` on the validation
host. Paths below are relative to that directory.

| Check | Current result | Evidence / outstanding work |
| --- | --- | --- |
| Host regression suite | **PASS: 831/831**, 147 suites, zero failures/errors | `host-pwm-tests.xml`, `host-pwm-tests.log`. |
| Generated configuration layouts | **PASS: 28 boards** | `layout-report.md`, `layout.json`; table dimensions, encodings and offsets retained; configuration grows by 60 bytes. Output payload 1508 bytes fits the 1536-byte blocking factor with protocol overhead. |
| Core8 ARM build | **PASS** | `feature/core8/`; memory figures below. |
| Proteus F7 ARM build | **PASS** | `feature/proteus-f7/`; memory figures below. |
| Core8 deployment identity | Final candidate flashed; matching INI verified, `engineType=99` | Bench finished with RPM zero and sensor mocks cleared. |
| Isolated bench behavior | **PASS: 55 samples**, no skips, errors or cleanup errors | `hardware/revision-bench.json`; final candidate `88f07ff8f0`, RAM-only configuration writes. Scope below. |
| Interactive TunerStudio | **PASS within the scope below** | `hardware/ts-live-report.md`, `hardware/ts-screenshots/`; actual 3.3.01 project connected, read controller, displayed independent live cursors and SD analyzer binding. No Auto Tune or calibration write. |
| Original firmware/configuration restoration | **PASS: byte-exact** | Original application `e2390abf2c`; two identical full 1 MiB flash reads match the backup, as do two runtime reads of all 25088 configuration bytes. RPM zero. `hardware/restored-flash.json`, `hardware/restored-identity.json`. |
| Running-engine calibration | **Not qualified** | Engine-specific airflow, thermal, transient and altitude behavior require separate measurements. |

### ARM resource results

`resources.json` records these link-time figures. RAM/heap deltas compare the
revision with the stage-3 baseline used by integration; they are not runtime stack
measurements.

| Board | Flash used / available | Flash free | Reported RAM section used / available | RAM free | Heap | RAM / heap delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Core8 | 681468 / 753664 | 72196 | `.ram4`: 60416 / 65536 | 5120 | 8240 | +336 / −396 |
| Proteus F7 | 727048 / 753664 | 26616 | `.ram3`: 66976 / 131072 | 64096 | 64096 | +336 / −336 |

Values are bytes. The F7 persistent wrapper occupies 29060 bytes, leaving 3708
bytes in its storage region. After the complete Core8 bench run, `threadsinfo`
reported 696 bytes free for MainLoop and 2620 bytes for ISR stack. These are the
observed margins for this isolated run, not worst-case engine workload bounds.

## Isolated bench coverage

The 55 recorded samples cover standalone SD/Alpha-N/MAF, authority 0/50/100%,
one common correction pass, shared temperature selection, pure-Alpha-N BARO,
standalone MAP multiplication and its exclusion from composite operation,
independent consumer cursors, MAP-estimate permission and measured-MAP rejection,
standalone sensor failure/recovery without rearm, composite fault/drain/stopped
rearm, endpoint qualification timing/invalidation, and both Idle VE owners crossed
with MAP/TPS axes plus exit/reentry. This uses simulated RPM and sensor mocks;
no running engine or exhaust response was measured.

Bench investigation led to two focused fixes: standalone admission now retains
its last completed publication during recalculation, while known failures still
close admission immediately; stopping a PWM callback now balances its nesting
counter across repeated simulator restarts. The final full run passed after both
fixes. Earlier partial runs are diagnostic history, not the final acceptance run.

## Interactive TunerStudio scope

A separate TunerStudio MS Ultra 3.3.01 instance used the exact candidate INI
and read the ECU. At simulated 1500 RPM, the SD editor and injection-phase
editor followed MAP 60 kPa; target lambda and cylinder-2 trim followed TPS 25%,
while cylinder-1 trim followed MAP. Offline checks covered model settings, Idle VE
selectors and unsupported raw load codes hidden from dropdowns. The SD analyzer
showed the correct table and live cursor with firmware endpoint 1.

All analyzer tabs and Start Auto Tune buttons remain visible; the INI active
condition is internal and is not exposed in the Advanced Settings filter panel.
The firmware qualification states were exercised in the 55-sample bench;
Auto Tune was not started and no exhaust response or analyzer-generated write
was tested. No Burn, Apply or Save on ECU was used. The isolated instance was
closed and its port settings cleared; the owner’s offline project and unsaved
edits were preserved.

TunerStudio logs a cosmetic warning for the second `INVALID` enum placeholder;
both unsupported values remain hidden. Existing deprecated `stft_deadband` and
duplicate boost-log-header parser warnings remain. The final INI was retained
byte-for-byte; the live report records its hash.

## Accepted requirements and regression coverage

Implementation paths below are relative to `firmware/`. Test names are exact
GoogleTest case names; the named files live under
`unit_tests/tests/ignition_injection/` unless another directory is shown. These
are representative regressions from the passing full suite, not a claim that
host execution qualifies engine calibration or tuning-software behavior.

| Requirement | Implementation | Representative test evidence |
| --- | --- | --- |
| §§1–2: dedicated maps always; remove manual model-readiness gates | `controllers/algo/airmass/` model dispatch and map validation; configuration/UI remove opt-in and readiness controls while retaining automatic fault admission | `test_dedicated_airmass_tables.cpp`: `MainMapOwnershipAndAxesDoNotFollowGlobalMode`, `UnusedModelAxesDoNotBlockTheActiveModel`, `DedicatedAxesMustBeStrictlyAscending`; `test_airmass_evaluation.cpp`: `LegacyModeIdsRemainStable`. |
| §3: model names, actual axes and editor sources; unchanged map storage | `integration/fome_config.txt`, `tunerstudio/tunerstudio.template.ini`, generated INIs; separate SD, Alpha-N and MAF calibration meanings | 28-board layout audit; dedicated-table tests above. Live labels/cursors verified; manual engine-tune migration is documented, not engine-qualified. |
| §4: general MAP-estimate permission, subordinate transient comparison, measured/effective distinction | `airmass/speed_density_airmass.cpp` captured MAP selection; permission defaults off and prevents estimate evaluation when off | `test_airmass_context.cpp`: `MapEstimatePermissionControlsFallbackAndTransientComparison`, `CapturePreservesMapProvenanceAndResolvesEveryLoadSource`; `test_blended_airmass.cpp`: `DisabledPermissionSkipsTransientComparisonEvenWhenStoredEnabled`, `EstimatedMapPermissionAndMeasuredOverrideRemainDistinct`. |
| §5: independent direct and indirect consumer loads | `controllers/algo/airmass_loads.cpp`, captured `AirmassLoadSnapshot`, per-consumer selectors and module call sites listed in the [inventory](blended-load-consumers.md) | `test_airmass_consumers.cpp`: `CylinderTrimsResolveEachSelectedCoordinate`, `PhaseStagingIatAndTrailingUseTheirOwnSources`, `SelectedSourcesAreIndependentAndUseCapturedFullPrecision`; `tests/actuators/test_fan_control.cpp`: `FanAcTablesHaveIndependentSourcesAndCursors`; `tests/actuators/test_boost.cpp`: `IndependentCorrectionAxesPreserveDriverIntentAndMissingSourcesUseSafeDuty`. |
| §5: active dependencies and coherent publication | `processAirmassConsumerLoads`, `isAirmassLambdaTargetRequired`, fuel conversion and `engine2.cpp`; full-precision snapshot validity, epoch/version/strategy checks | `test_airmass_consumers.cpp`: `StaleCaptureCannotRepublishAfterStopOrLiveWrite`, `CaptureChecksBurnVersionAndActiveStrategy`, `DisabledInjectionStillRequiresTargetWhenStftUsesIt`, `DisabledInjectionStillRequiresTargetForActiveLambdaProtection`, `DisabledIgnitionStillRequiresSourceForActiveGppwmAlias`; `test_airmass_context.cpp`: `DisabledIgnitionDoesNotRequireItsStoredMapOrRunTimingTables`, `UnusedLambdaTargetDoesNotBlockAirCalculationOrPublishFuel`, `StandaloneFuelConversionRejectsInvalidTargetBeforePackedPublication`. |
| §6: attributable endpoint VE Analyze | Session qualification in `airmass_loads.cpp` and matching INI bindings; whole authority table uniformly 0% or 100%, startup delay, Idle VE disabled, invalidation on live changes/faults | `test_airmass_consumers.cpp`: `RequiresStartupDelayUniformAuthorityAndNoIdleTable`, `MovingTuneWriteCannotRequalifyThroughZeroOrInvalidRpm`, `TunePreparedAtConfirmedStopCanQualifyAtNextStart`. Live SD analyzer binding verified; internal active conditions are not shown in the GUI filter panel. Actual exhaust feedback and Auto Tune writes were not exercised; moving/mixed endpoints are unsupported. |
| §7: one Idle VE owner, independent four-choice axis, taper before blend | `airmass/airmass.cpp`, SD/Alpha-N branch evaluation; `IdleVeLoadSource` offers model default, measured MAP, TPS, effective MAP and excludes circular final filling | `test_airmass_context.cpp`: `IdleVeBelongsToOneBranchAndUsesItsOwnAxis`; `test_blended_airmass.cpp`: `IdleOverlayRunsOnlyInsideItsContributingModel`; `test_airmass_evaluation.cpp`: `IdleTaperThenCompoundsCorrectionsAndPublishesCapturedAxes`; `test_dedicated_airmass_tables.cpp`: `MafIdleRetainsItsCorrectionMeaningAndIgnoresIdealGasOptions`. |
| §8: one captured Tcharge/IAT selection shared by SD and Alpha-N | `AirmassInputs` capture and both ideal-gas models; selected source/value diagnostics; existing Tcharge estimator and fallback contract retained | `test_airmass_context.cpp`: `SelectedTemperatureValidityIsSharedByBothModels`, `RawModelsAndCorrectionsUseOneSnapshotAndMatchStandaloneResults`; `test_blended_airmass.cpp`: `SharedTemperatureAndBaroApplyBeforeBlendRegardlessOfStoredHybridOption`. |
| §9: standalone-only Alpha-N Multiply MAP | Explicit `AlphaNPressurePolicy` in `airmass/alphan_airmass.cpp`; composite always requests pure reference pressure | `test_airmass_context.cpp`: `HybridPressurePolicyIsExplicitAndSuppressesAutomaticBaro`; `test_blended_airmass.cpp`: `SharedTemperatureAndBaroApplyBeforeBlendRegardlessOfStoredHybridOption`. |
| §10: optional pure-Alpha-N BARO correction before blend | Captured continuous/startup BARO and explicit stable reference; pure Alpha-N density ratio defaults off, is suppressed for hybrid Alpha-N, and does not affect SD/MAF | `test_airmass_context.cpp`: `BaroReferenceAndCapturedPressureAreValidated`, `HybridPressurePolicyIsExplicitAndSuppressesAutomaticBaro`; `test_blended_airmass.cpp`: `SharedTemperatureAndBaroApplyBeforeBlendRegardlessOfStoredHybridOption`, `EndpointsSkipUnusedMapAndBaroDependenciesButShareTemperature`. |
| Cross-cutting: exact endpoints, one correction pass, strict standalone faults and safe injection admission | Raw branch evaluation, final correction/publication, strict fuel conversion and `AirmassInjectionState` | `test_blended_airmass.cpp`: `FlatAuthorityEndpointsStayExactAcrossFractionalInputs`, `CorrectionsApplyOnceAndLoadsResolveFromFinalComposite`, `RequiredSensorFaultBlocksSchedulingDespiteAllFuelAdditions`; `test_airmass_evaluation.cpp`: `MafRejectsNonfiniteInputsBeforeInterpolationOrPublication`; `test_airmass_consumers.cpp`: `InvalidAirmassCannotBeRevivedByLuaFuelOrInjectorLag`; standalone/composite scheduler regressions in `test_airmass_injection_gate.cpp`. |
| Bench regression: scheduling during standalone recalculation | `controllers/engine_cycle/airmass_injection_state.cpp`, token-checked failure rejection in `controllers/algo/fuel_math.cpp` | `test_airmass_injection_gate.cpp`: `StandaloneKeepsCompletedFuelAvailableDuringRecalculation`, `StandaloneRecalculationCannotRetainFuelAcrossInvalidRpmOrVersion`; `test_airmass_context.cpp`: `StandaloneInputFailureClosesAdmissionBeforeFinalPublication`, `StandaloneFuelConversionRejectsInvalidTargetBeforePackedPublication`. |
| Bench regression: repeated PWM stop/restart | `controllers/system/timer/pwm_generator_logic.cpp` balances callback nesting on stop | `unit_tests/tests/test_pwm_generator.cpp`: `PWM.RepeatedStopRestartBalancesCallbackNesting`, 40 stop/restart cycles. |

## Qualification limits

- All map dimensions and encodings remain unchanged. Table resizing, PR #791
  integration and shared map-storage slots were excluded.
- Existing tunes need explicit backup and manual restoration of calibrations and
  actual source choices. New selector defaults do not convert axes or calibrate
  maps; automated conversion scripts were excluded.
- Host tests establish arithmetic, dependencies and state transitions for their
  fixtures. ARM builds establish compilation and static resource fit. Neither
  establishes engine calibration, airflow accuracy, transient fueling, thermal
  equivalence, altitude behavior or suitability for boosted operation.
- BARO compensation of pure Alpha-N is implemented and defaults off. BARO
  normalization of the MAP-estimate table remains study-only; there is no
  additional estimate correction or mass multiplier from that proposal.
- Composite VE Analyze is restricted to qualified uniform endpoint sessions.
  Mixed authority, moving endpoint attribution and Idle VE analysis are outside
  the implemented analyzer scope.
- Derived GPPWM outputs are sampled values; a finite derived value does not
  prove every upstream sensor is healthy. See the consumer inventory for this
  boundary and for physical outputs whose units remain fixed.
- Original firmware/configuration restoration is recorded separately below. The
  isolated bench and interactive editor checks do not qualify engine calibration
  or delayed exhaust feedback for VE Analyze.

## Original ECU restoration and final workspace

After the GUI check, simulation was stopped and mocks cleared. The original
application and calibration were restored through OpenBLT, preserving the
bootloader. Two full 1 MiB reads match the pre-test backup byte-for-byte:

- Firmware: `FOME core8 20260929@e2390abf2c`.
- Signature: `rusEFI (FOME) capoworks.2026.09.29.core8.3263625221`.
- Full flash SHA-256: `e61982ff60a5475c7c233c0907fa6fc9f4cb3acff783792edda09c192920cc12`.
- Configuration: 25088 bytes, two identical runtime reads matching the backup.
- Configuration SHA-256: `c90dc06ca762abdd3e9650356bc696ee60a84c09e2cda33e5811c2263e8b289f`.
- Final RPM: zero; hardware scripts closed the serial port.

Logs, verification scripts and images remain in the artifact directory. Nine
live GUI screenshots are archived in `hardware/ts-screenshots/`. Generated files
were archived and restored as required by `CLAUDE.md`; source templates and
generators remain committed. Builds regenerate their matching INIs. The original
`.vscode/settings.json` and `java_console/luaformatter` IDE changes were preserved.
No branch or PR was published.

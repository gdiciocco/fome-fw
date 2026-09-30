# Air estimation and load architecture

## Objective and status

Design direction updated on 2026-09-28. Provide a maintainable air-estimation
architecture for FOME, with the owner's naturally aspirated ITB engine as the
first engine qualification case. Success means repeatable fueling, understandable
calibration and bounded embedded resource use across supported configurations.
Any claim of improved performance requires comparison with existing strategies.

The revised calculation is implemented on `feature/blended-airmass` as of
2026-09-30: dedicated maps are the normal path; Idle VE has explicit model
ownership and its own axis; SD and Alpha-N share a selected temperature;
standalone Alpha-N can multiply MAP; pure Alpha-N can use an optional BARO
factor before blending. Independent downstream selectors use a retained input
context. There are no manual map-readiness activation declarations.

The [revision requirements](blended-airmass-revision.md) take precedence over
earlier control and load-selection decisions. The
[input contracts](airmass-input-contracts.md) describe current equations,
validity and publication boundaries. See the
[validation record](blended-airmass-validation.md) for the dated scope of host,
board and isolated bench evidence; earlier evidence does not validate new
revision behavior. Engine qualification remains outstanding. Table resizing,
MAF blending and automatic tuning of both maps in a mixed region are excluded.

## Responsibilities

| Responsibility | Contract |
| --- | --- |
| Capture inputs | Read a consistent set of required RPM, pressure, TPS and temperature inputs for one calculation, retaining validity and measured/estimated source. Do not copy maps or all engine state. |
| Evaluate models | Each model owns its calibration and returns grams per cylinder per engine cycle, validity and its native load. Table axes and pressure/temperature reference are explicit. |
| Select contribution | A calibrated policy chooses SD/Alpha-N weights. It does not modify model physics, calibration tables or downstream table axes. |
| Apply shared corrections | Apply each common correction once at its documented stage. Keep each correction at its defined stage, with strict required-input validity. |
| Resolve load coordinates | Lambda, ignition and other consumers obtain their configured coordinate with known units, source and validity. A load coordinate is not automatically the selected model's native load. |
| Publish and diagnose | The live fuel calculation owns publication and fault state. Dry queries cannot change them. Capture detailed diagnostics only when requested. |

Use the existing model classes and small value types. Add explicit source/unit
metadata where values cross these boundaries; do not introduce a runtime graph,
dynamic allocation or a general estimator registry. Consistent input capture
does not imply atomic sampling of asynchronous sensors or atomic publication.
Document acquisition boundaries and use the sensor layer's validity rules.
Derived correction inputs also need a documented sampling stage: existing GPPWM
fuel/ignition-load channels can refer to the previous published calculation.
Keep that behavior explicit instead of introducing partial publication or a
circular dependency on the mass currently being corrected.

SD VE, Alpha-N filling and the MAF correction factor have different meanings.
Their percentage values must not be averaged and presented as a measured common
VE. Keep MAF a supported standalone estimator with its own map and regression
coverage. Adding it to the blend requires separate qualification.

## Decisions for the first composite

### Independent calibration and observable transitions

Use independent 16 x 16 model maps and axes, including fractional TPS bins.
The 8 x 8 RPM/TPS authority map only sets the Alpha-N contribution. Keep 0% and
100% exact; skip the unused mass branch. Log whether a branch was evaluated so
an absent branch value cannot be confused with a zero mass or a fresh estimate.

Keep weight selection separate from sensor validity. A valid MAP sensor can be
a poor calibration coordinate in part of the operating range. High MAP, low
MAP or disagreement between estimators does not establish which model is right.
Do not adjust weights automatically from lambda error or model disagreement.

Tune the models individually in their intended regions, inspect agreement in
the overlap, then qualify the transition. One lambda residual cannot uniquely
identify errors in two maps and their mixing weight. Automatic tuning of both
maps in the mixed region stays disabled. A diagnostic-only comparison mode can
be added later with measured CPU/stack cost and no effect on live control.

### Explicit loads and dependencies

Every independently calibrated load consumer has its own source selection or
retains an existing independent selector. This includes tables, thresholds and
control logic that formerly inherited another function's load. The retained
snapshot supplies source validity as well as values. Its native Default meaning
is explicit, and an effective-MAP option is available independently of Default.

| Selection | Source and units |
| --- | --- |
| Default | Standalone SD effective MAP (kPa), Alpha-N TPS (percent), or MAF uncorrected filling (percent); composite effective MAP (kPa). |
| MAP | Captured measured MAP, kPa. |
| TPS | Captured TPS1, percent. |
| Accelerator pedal | Captured pedal position, percent. |
| Cylinder filling | Final air mass divided by standard cylinder charge, percent. |
| Effective MAP | Captured effective MAP, kPa, including a permitted valid estimate. |

Required sources must be valid. A valid estimate cannot satisfy measured MAP,
and a numeric native-load value cannot hide an invalid SD pressure or Alpha-N
TPS. Changing one consumer does not change another consumer, a model's main
axis or the authority table. Changing units requires recalibrating axes and
thresholds, not relabeling bins.

The live owner calls `processAirmassConsumerLoads` after obtaining final mass
and before accepting the fuel calculation. It validates the union of active
consumer dependencies and retains a compact snapshot for later lookups. Dry
queries validate without changing live publication or fault state. Corrections
that historically read previous fuel/ignition load retain that sampling stage.

MAP-independent operation at 100% Alpha-N requires all active consumers and
corrections to resolve without MAP. Choosing TPS only for lambda and ignition
is insufficient. SD still requires effective MAP whenever it contributes.
Each independently calibrated VVT table, trim, monitor and control threshold
must have coherent source, units, cursor, activation and validity checks; the
consumer inventory records those details. Diagnostic/protocol values that
promise pressure or filling retain that physical meaning independently of a
table's selected coordinate.

Normalized cylinder filling is a useful existing candidate derived from final
air mass and a defined standard charge. It inherits errors in that air estimate;
it is not an independent measurement of torque or a universally preferable axis.

### Idle and changing conditions

The single Idle VE calibration can replace SD VE or Alpha-N reference filling
inside its selected contributing branch before blending. Its lookup axis is
independent of that ownership. The other branch retains its main map. At zero
contribution the selected branch and its idle-only dependencies are skipped.
Standalone SD, Alpha-N and MAF retain their own idle overlay behavior through
an explicit load selection; the composite owner selector does not redirect a
standalone model's table.

Existing idle/cranking-taper activation and the driver-intent transition remain:
full idle-table contribution through half the deactivation threshold, linear
return to the main table by the full threshold. Changing the axis changes the
lookup only. Main-map analyzer samples affected by Idle VE or its delayed lambda
response remain unqualified. Do not infer idle merely from low RPM or prescribe
SD at idle for every ITB engine.

Include changes in idle bypass opening at fixed TPS in qualification. A future
bypass-air model needs a calibrated flow relationship and pressure/temperature
inputs; valve duty alone is not measured flow. Define which mass already includes
that air before applying any added term. Dedicated idle maps are a separate
calibration feature, not proof that bypass airflow is accounted for.

One Tcharge/IAT selection supplies the same captured Kelvin temperature and
validity to SD and Alpha-N. Standalone operation uses that selection for the
active ideal-gas model. Tcharge retains its existing estimator fallbacks, rate
limits and previous-calculation airflow input; measured IAT requires a valid
sensor. MAF receives no extra ideal-gas correction from this setting.

Pure Alpha-N uses 101.325 kPa in its base equation. Standalone Multiply MAP
explicitly substitutes effective MAP for that term and requires a valid
pressure. The composite always requests pure Alpha-N, regardless of the stored
standalone option. Main Alpha-N axes stay TPS/RPM in either case. A map tuned
for hybrid pressure physics is not automatically calibrated for pure blending.

When enabled, the pure Alpha-N branch is multiplied by captured ambient BARO
relative to a stable configured calibration reference before blending. Hybrid
standalone Alpha-N, SD and MAF do not receive this factor. A zero-contribution
Alpha-N branch adds no BARO requirement. The existing common barometric fuel
table remains a later residual correction; review it to avoid repeating the
same density effect.

`Use MAP estimate table` is a default-off permission. With it off, neither
fallback nor transient comparison evaluates the estimate. With it on, a valid
estimate may replace unavailable measured MAP; its subordinate transient option
also permits the existing greater-of-measured-and-estimated comparison. The
measured source remains separate. The
[MAP-estimate BARO study](map-estimate-baro-study.md) proposes further work only;
no normalisation factor is applied to the estimate in this revision.

Temperature, BARO and pressure-policy changes require explicit calibration
review. The old fixed-20-C Alpha-N mode and independently selected branch
temperatures have no general equivalent under the new shared selector.
Validate warm-up, heat soak and ambient-pressure changes on the engine rather
than treating these equations as complete environmental compensation. Fuel
film, acceleration enrichment and idle-bypass airflow remain separate concerns.

### Faults and resource limits

Preserve the detailed activation, injection-inhibit and recovery contract in
the implementation plan. Separate sensor validity, estimate provenance and runtime readiness in
diagnostics. Strategy selection enables operation; no readiness checkbox
certifies the maps. No automatic promotion of a surviving
model is allowed without a separately qualified full-range fallback map.

Continue using compact results and optional diagnostics. The original 1,248-byte
map/axis payload was a calibration-storage estimate, not the total RAM cost.
The revision adds selectors and compact diagnostics without resizing tables.
Measure generated layouts, RAM, flash, stack and execution time with both models
active. Include diagnostic logging paths and every supported board constraint.

## Subsequent extensions

These are candidates with separate evidence requirements, not promised features
of the first composite delivery:

| Extension | Evidence required before adoption |
| --- | --- |
| ITB transition based on pressure relative to ambient/airbox | Logs across RPM, throttle and barometric conditions; no unintended return to SD when sampled MAP dips at high throttle; explicit hysteresis/filter behavior if used. |
| Explicit idle/bypass airflow support | Measured or calibrated flow behavior and proof that existing SD mass is not counted twice. |
| Airbox-pressure use and MAP-estimate barometric normalisation | Separate source/reference definitions and engine evidence beyond the next revision's accepted Alpha-N BARO and Tcharge/IAT requirements. |
| VVT, variable intake or additional model dimensions | Repeatable residuals showing the existing surfaces cannot cover the changing configuration, plus a bounded storage/UI design. |
| MAF/model blending or automatic fallback | Sensor transport/timing behavior, calibration coverage of the fallback model, fault-transition behavior and resource measurements. |

## Qualification

Agree operating ranges and acceptance tolerances before comparison. Retain the
best calibrated standalone mode as a baseline. Compare with the composite using
the same injector characterization, fuel pressure model, lambda measurement and
environmental corrections. Record any changes that prevent a direct comparison.

| Case | Record and assess |
| --- | --- |
| Steady load across RPM/TPS/MAP | Target/measured lambda, repeatability, closed-loop correction demand, model masses, weight, selected loads and validity. Account for exhaust transport delay. |
| Transition overlap in both directions | Mass and pulse-width continuity, estimator disagreement, authority changes and lambda excursions. Smooth weights alone do not guarantee an accurate transition. |
| Idle and return to idle | RPM stability, lambda, bypass/DBW position, accessory load, warm-up and stall/restart behavior. |
| High throttle with sampled MAP dipping | No unintended policy switch or load-coordinate discontinuity. This does not imply that a real pressure change should have no effect on SD mass. |
| Thermal and barometric changes | Hot/cold repeatability and correction behavior, with the tested environment and sensor placement recorded. |
| Sensor loss and recovery | Required-input behavior, injection gating, queued-pulse drain and rearm, matching the documented fault policy. |
| Embedded execution | Endpoint and mixed-mode CPU time, stack watermarks, logging overhead and resource deltas on supported targets. |

Use host tests for equations, dependencies and publication; simulator/log replay
for reproducible input sequences; bench tests for scheduling and fault behavior;
engine tests for calibration quality. Replay using synthetic inputs does not
validate the real engine's lambda response. One engine establishes one qualified
application, not universal ITB or boosted-engine support.

## Upstream delivery

The original sequence below remains a review aid. Revision migration is an
explicit manual calibration procedure; no automatic conversion is promised for
incompatible axes, temperature choices or pressure policies.

Keep changes useful and reviewable at working boundaries, following
`CONTRIBUTING.md`. The integration branch can carry the sequence before PRs are
split. Do not expose an incomplete composite as a selectable runtime mode.

1. Compact evaluation and optional diagnostics, preserving legacy results
   (implemented and locally tested; upstream submission remains separate).
2. Independent calibration maps, axes, conversion and standalone UI/analyzer
   behavior, preserving all three existing strategies.
3. Input/correction/load contracts with regression coverage and unchanged legacy
   behavior. Split this further if the consumer audit exposes independent fixes.
4. Complete SD + Alpha-N operation, including strategy selection, fault handling,
   diagnostics, UI, resource evidence and a reproducible tuning procedure.

Attach engine qualification evidence as it becomes available; label automated,
bench and engine results separately. Maintainer review determines upstream scope
and acceptance; this design does not imply maintainer endorsement.

## External references and lessons

- [MoTeC CTN0036](https://www.motec.com.au/hessian/uploads/CTN_0036_Multi_Throttle_Tuning_Method_4f4c494a0a.pdf)
  describes ITB tuning using estimated port pressure and explicit compensation
  for pressure before the throttles. It motivates documenting physical reference
  conditions and keeping the tuning procedure manageable, not copying its model.
- [MoTeC GPR Motorcycle](https://www.motec.com.au/products/GPR%20Motorcycle)
  documents separate efficiency tables associated with throttle and manifold
  pressure. Its combination is not evidence that our mass blend is equivalent
  or superior.
- [MaxxECU ITB setup](https://www.maxxecu.com/webhelp/solutions_and_faq-engines_with_individual_throttle_bodies.html)
  discusses TPS axes for VE and, where useful, lambda and ignition. Its
  [fuel calculation documentation](https://www.maxxecu.com/webhelp/settings-fuel-fuel_inj_general.html)
  distinguishes the table axis from whether MAP enters the equation.
- [Reported MegaSquirt ITB transition problem](https://www.msextra.com/forums/viewtopic.php?p=530942)
  includes an owner's logs of a sampled-pressure dip at high throttle interacting
  with a pressure threshold. Use this as a regression scenario for future policy
  work, not a claim about all versions or configurations of that system.

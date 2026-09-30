# Study: barometric normalisation of MAP estimate

## Status and scope

**Study only — no MAP-estimate BARO normalisation is implemented or enabled by
this revision.** This records the separate investigation accepted in
[revision section 11](blended-airmass-revision.md#11-study-of-barometric-normalisation-of-map-estimate),
updated 2026-09-30. It is distinct from the implemented optional pure-Alpha-N
BARO mass factor.

The existing estimate table stores absolute kPa against TPS and RPM. Its values
are unchanged by ambient pressure. The implemented resolver can use that table
only when `Use MAP estimate table` permits it, either as fallback or for the
subordinate transient comparison. Measured MAP is preserved separately.

The following equations are engineering hypotheses and proposed qualification
steps. They do not establish an engine-accurate airflow relationship, including
on naturally aspirated ITB engines.

## Hypothesis and derivation

Let:

- `p_table(TPS, RPM)` be the calibrated absolute MAP estimate;
- `b_ref` be ambient pressure when that estimate calibration was obtained;
- `b_now` be the current valid ambient-pressure sample;
- `r(TPS, RPM)` be the hypothesized repeatable ratio of MAP to ambient pressure.

Assume, for a qualified operating region and otherwise comparable conditions,
that `r` is approximately independent of ambient pressure:

```text
r(TPS, RPM) ≈ p_table(TPS, RPM) / b_ref
p_estimated_now ≈ r(TPS, RPM) * b_now
                = p_table(TPS, RPM) * b_now / b_ref
```

The ratio `b_now / b_ref` is dimensionless because both pressures are absolute
and use the same units. For example, a table value of 60 kPa calibrated at
100 kPa BARO would predict 48 kPa at 80 kPa BARO. That numerical result follows
from the assumption; it is not evidence that the engine actually reaches it.

This approximation is a candidate for naturally aspirated regions where the
measured MAP/BARO ratio is repeatable at the same RPM/TPS and comparable air
path, temperature, valve timing and accessory conditions. Identify such
regions from engine measurements before applying the approximation across a
whole calibration.

### Why a single ratio may be inadequate

A TPS/RPM pair does not fully specify the air path or cylinder charge. The
study must test effects of idle-bypass opening at fixed TPS, throttle geometry,
pressure-dependent flow, transients, manifold filling, heat soak, VVT and intake
configuration. Treat nonlinear throttle-flow behavior and possible changes of
flow regime as competing explanations when the normalized curves disagree;
do not force a linear ambient multiplier to fit those observations.

Boosted operation is outside the initial qualification. Even a simple regulated
pressure target can contradict the assumed constant ratio. For a hypothetical
constant gauge boost target `B`, absolute MAP would be `b_now + B`, giving
`MAP / BARO = 1 + B / b_now`, which changes with ambient pressure. A fixed
absolute pressure target provides another counterexample. Compressor control,
pressure limits and airbox pressure would require their own explicit model and
evidence. Do not extend naturally aspirated findings to boosted regions.

The BARO input denotes ambient pressure. Substituting airbox pressure requires
an explicit source choice and separate qualification. Neither sensor naming
nor this derivation makes those pressures interchangeable.

## Proposed calibration and dependency contract

If engine evidence supports adoption, introduce separate default-off controls
for estimate normalisation and its stable reference pressure. Those controls
would be subordinate to the existing general estimate permission.

The MAP-estimate reference belongs to the estimate table. It is independent of
`alphaNBaroReferencePressure`, which belongs to the Alpha-N calibration. The
maps can have been calibrated at different pressures; sharing one reference
would silently change one of their calibration meanings. Neither reference
should be reset to the latest startup BARO. Pressure units, measurement source
and calibration conditions must be recorded with each reference.

A future implementation should capture one ambient sample and its validity for
each calculation. Continuous BARO and pressure captured at startup must remain
distinguishable in diagnostics. Startup pressure alone cannot track later
ambient changes during the same run.

Proposed dependency rules:

| Situation | New BARO/reference dependency through this feature |
| --- | --- |
| General estimate permission off | None; do not evaluate the estimate or normalisation. |
| Normalisation disabled | None; preserve the permitted existing estimate. |
| Healthy measured MAP, no requested transient comparison | None; the estimate is unused. |
| Normalisation enabled and estimate required for fallback/comparison | Require valid captured BARO, finite positive reference, and valid corrected estimate. |

If a required coefficient or corrected estimate is invalid, follow the
applicable required-input fault policy. Do not silently substitute standard
BARO, switch off normalisation, use the uncorrected table as a fallback, or
modify measured MAP. Validity and bounds for the supported engine region must
be defined before implementation; multiplication alone does not establish a
plausible pressure.

## Proposed calculation placement

A future implementation would evaluate in this order:

1. Capture measured MAP, TPS, RPM and any conditionally required ambient input.
2. Determine whether the permitted estimate is needed.
3. Validate axes, cells and inputs; interpolate the original absolute table.
4. If independently enabled and qualified, multiply that estimate by
   `b_now / map_estimate_reference`.
5. Validate the corrected estimate, then apply the existing fallback or
   greater-of-measured-and-estimated transient selection.
6. Publish one effective MAP and its provenance for all consumers.

Apply normalisation **before** fallback selection and transient comparison.
Otherwise the resolver can select the wrong pressure. Measured MAP remains the
original sensor sample throughout. SD, hybrid standalone Alpha-N and every
consumer requesting effective MAP must see the same selected corrected value;
an explicitly measured-MAP consumer must still receive the measured sample.

SD and hybrid Alpha-N already use effective pressure in their mass equation.
Do not apply this same estimate coefficient again to their output mass.
Pure Alpha-N retains its separate, independently referenced BARO factor. The
common barometric fuel table stays a residual correction whose calibration
must be reviewed for overlap; it must not be silently rewritten.

Proposed diagnostics would retain the original table estimate, corrected
estimate, applied coefficient, ambient source/validity and final pressure
selection. These are study requirements, not currently available output fields.

## Evidence required before adoption

Establish operating ranges, sensor uncertainty and allowable pressure/lambda
errors before examining results. Retain a baseline with normalisation disabled.

On the engine, collect comparable runs at multiple ambient pressures. Log RPM,
TPS, measured MAP, BARO, selected temperatures, bypass/DBW position, VVT,
authority, both model masses and lambda target/measurement. Account for the
lambda measurement delay. Include:

- idle and changing bypass opening at fixed TPS;
- steady partial throttle and full throttle;
- opening and closing transients, including measured-versus-estimated ordering;
- warm-up and heat soak at comparable load;
- the model's complete intended RPM/TPS region and any intake configuration changes.

Compare the uncorrected estimate, the proposed corrected estimate and measured
MAP at each condition. Examine whether `measured_MAP / BARO` collapses to a
repeatable surface, where it does not, and whether interpolation across the
qualified regions remains acceptable. Also compare resulting lambda behavior;
a closer pressure estimate alone does not validate all fueling corrections.

Host tests could establish arithmetic, source separation, branch independence,
reference validation and correction placement. Bench tests could establish
stored settings, diagnostics, transient selection and fault/recovery behavior.
**Neither host simulation nor an isolated bench can qualify the hypothesis on
an engine or establish altitude compensation accuracy.**

Adoption requires recorded engine evidence and a separate reviewed change with
its own defaults, migration instructions, tests and resource measurements.
Until then, retain the unnormalised absolute estimate and current permission
contract. Table resizing remains excluded.

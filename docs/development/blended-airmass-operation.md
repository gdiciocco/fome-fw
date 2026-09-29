# SD + Alpha-N operation

This document describes the composite implementation on `feature/blended-airmass`.
See [input contracts](airmass-input-contracts.md) for the model, correction and
load boundaries. Engine calibration and comparative engine qualification remain
separate from software and isolated ECU bench tests.

## Calculation and calibration

`LM_SD_ALPHA_N = 4` blends grams per cylinder from the independently calibrated
16 × 16 SD and Alpha-N maps. The 8 × 8 RPM/TPS table specifies Alpha-N authority:
0% evaluates SD only, 100% evaluates Alpha-N only, and intermediate values
evaluate both. Shared VE corrections apply once after blending. MAF remains a
standalone strategy with its own map.

Enable dedicated maps, select natural VE axes (`veOverrideMode = None`), disable
the shared idle VE table, and mark both model maps ready only after calibration.
Defaults and conversion leave all readiness flags false and authority at 0%.
Both model readiness flags are required even at an authority endpoint. TPS is
always required for authority lookup; effective MAP is always required for the
default load. An inactive mass branch does not require its additional sensors.

The existing MAP estimate can supply effective MAP only when its calibration is
marked ready and its inputs and table are valid. Transient MAP comparison also
requires that readiness, even when measured MAP wins the comparison. There is
no automatic promotion of a surviving mass model after a required input fails.

Default fueling load remains effective MAP in kPa. Lambda and ignition selectors
independently resolve captured effective MAP, measured MAP, TPS, pedal or final
normalized cylinder filling. An explicit measured-MAP selector cannot use the
estimate. A configured HPFP also requires measured MAP, because its pressure
target table uses that source independently of composite fueling load.

Staged injection follows the resolved lambda coordinate at full float precision.
The old packed `afrTableYAxis` diagnostic saturates above 655.35 in composite
mode; `blendedLambdaLoad` retains the full value and drives the composite lambda
and staging cursors. Lambda monitoring continues to use `fuelingLoad`.

## Injection admission and recovery

| State | New normal injection | Recovery |
| --- | --- | --- |
| Standalone | Existing strategy policy | Existing behavior |
| Waiting for valid fuel | Inhibited | A complete, current positive-RPM fuel calculation |
| Ready | Allowed subject to existing limiters | Remains ready while valid |
| Fault latched | Inhibited | Stop, drain accepted callbacks, explicitly rearm, then calculate valid fuel |

Required-input, calibration, correction, result, load, strategy-change and
scheduling faults have separate diagnostics. The live fuel calculation owns
readiness; a dry model query cannot unlock injection. Readiness requires final
cylinder fuel and scheduling fields to be valid and published together. Fixed
cranking fuel, acceleration enrichment and Lua additions do not bypass the gate.

Live TunerStudio writes invalidate an in-progress calculation. Switching into
or out of composite mode while running or while callbacks remain pending latches
a strategy fault. Switching away and back does not clear an existing latch.

Already accepted pulses open and close normally, including overlapping and
staged pulses. Fault handling prevents new sequences and split continuations;
it does not discard callbacks or reset injector overlap accounting. Priming is
disabled in composite mode. A delayed standalone prime rechecks admission at
its actual start; an already accepted prime closes its captured output mask.

The scheduler reserves every callback in a sequence before accepting any of
them. Each newly accepted deadline must be less than ten seconds in the future.
This bounds the queued deadlines relative to a fault; it is not a guarantee
that a stalled processor physically executes the callbacks within that time.
Rearm checks the actual pending count and never infers completion from elapsed
time.

Use the TunerStudio rearm button or console `rearm_airmass` after the engine is
physically stopped and accepted callbacks have drained. Zero displayed RPM
alone is insufficient while the trigger state is still spinning up. Rearming
composite mode returns to Waiting; the next complete positive-RPM calculation
must establish readiness. Existing fatal-error and watchdog safing remain active.

## Diagnostics and tuning

Per-model mass, native load and map values accompany requested/effective
authority, correction multiplier, injection state and first latched fault.
Mass channels display milligrams in TunerStudio and contain grams on the wire.
The legacy single VE and its axis are zero in composite mode because the model
map values have different physical meanings.

`blendedFlags` defines the validity of model diagnostics:

| Bit value | Meaning |
| --- | --- |
| 1 / 2 | SD / Alpha-N was evaluated |
| 4 / 8 | SD / Alpha-N result was valid |
| 16 | Effective MAP used an estimate |
| 32 | Composite air calculation was valid |
| 64 | MAP estimate was evaluated |

The calculation-valid flag describes the air calculation, not final injection
admission; use the injection state for that decision. Effective authority is
zero on a failed air calculation and meaningful only with its validity flag.
An unevaluated branch value is unavailable, rather than a measured zero mass.

VE Analyze is disabled for the composite. Calibrate each model in its intended
region, compare masses in the overlap, then tune authority. Review all MAP-based
default-load tables when converting an Alpha-N tune. Endpoint mass equivalence
does not imply identical pulse widths when lambda or downstream loads differ.

## Tune compatibility

The flash format changes; a raw old ECU tune is not migrated in place. Back up
the original MSQ and matching definition before updating. The explicit
[conversion tool](../../misc/airmass_conversion/README.md) preserves the original
standalone strategy and initializes new controls without activating blending.
Restore using the matching firmware definition, inspect the result, and prepare
the second model and any estimate before marking them ready.

Existing persistent offsets are intended to remain stable. The authority axes,
table and readiness word add 100 bytes beyond the independent-map stage.
New output channels add 32 bytes at wire offset 532; later existing live-data
offsets move by 32 bytes, so logging and TunerStudio require the matching INI.
Generated layout audits and ARM measurements
must confirm these totals; calibration growth alone does not measure total RAM,
stack or execution cost.

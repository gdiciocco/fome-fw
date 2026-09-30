# Blended Speed Density and Alpha-N development plan

## Status and scope

Design started on 2026-09-27. The branch now implements independent model maps,
shared input capture and SD + Alpha-N blending. Software, ARM build and isolated
Core8 bench evidence is recorded in [validation](blended-airmass-validation.md).
Engine calibration and comparative engine qualification remain outstanding.

The [2026-09-29 revision requirements](blended-airmass-revision.md) define the
current revision: dedicated maps by default, simpler activation and names, an
explicit MAP-estimate policy, independent load selection for every consumer,
Idle VE with a selectable load axis, Tcharge/IAT selection shared by both
blended models, optional MAP multiplication for standalone Alpha-N only,
optional barometric compensation for pure Alpha-N before blending, and qualified
endpoint VE Analyze. MAP-estimate barometric normalisation is recorded separately
as a study requiring qualification before adoption.
They take precedence over conflicting
first-delivery decisions below. Table resizing is excluded. Implementation of these revisions is in progress, with verification tracked in
[the implementation record](blended-airmass-implementation.md). Earlier stage-3
results below qualify only the first delivery.

- Branch: `feature/blended-airmass`.
- Base: upstream `FOME-Tech/fome-fw` `master`, fetched on 2026-09-27,
  `ede26b3702126e86fc956543e8ff0e9d4a986aa6`.
- Worktree: `/home/deffie/worktrees/fome-blended-airmass`.
- Architecture and difficult reviews: GPT-6 Astra, high reasoning.
- Bounded configuration, UI and test work: GPT-5.6 Sol, high reasoning.
- First delivery: independently calibrated SD, Alpha-N and MAF modes, plus an
  explicit SD + Alpha-N mode that blends calculated air mass.
- MAF remains a supported standalone mode with its own calibration. Blending
  MAF with another model and simultaneous three-model blending are later work.

The first implementation uses 16 x 16 strategy maps. An 8 x 8 authority table
controls the transition between SD and Alpha-N. These are separate dimensions:
the authority table does not limit either model's calibration to 8 x 8.

On 2026-09-28 the project objective was expanded: qualify the owner's naturally
aspirated ITB engine while providing an architecture useful to FOME upstream.
[Air estimation and load architecture](airmass-architecture.md) defines that
direction, the boundaries of the first delivery and comparative acceptance
criteria. The design below records the original implementation sequence and
its initial MAP load default. The revision requirements define the next
contract for independently selected consumer loads and sensor dependencies.

## Prior work

The following issues and PRs were inspected, including their discussions or
diffs. Their states below were checked on 2026-09-27.

| Reference | State | What it contributes |
| --- | --- | --- |
| [FOME #459](https://github.com/FOME-Tech/fome-fw/issues/459) | Open | Requests one VE map per strategy for switching and blending. |
| [FOME PR #539](https://github.com/FOME-Tech/fome-fw/pull/539) | Open | Adds SD, MAF and Alpha-N tables, but shares axes and selects a table using the global strategy. It does not blend air models. |
| [FOME #248](https://github.com/FOME-Tech/fome-fw/issues/248) | Open | Discusses SD at low load, TPS at high load, independent axes, and replacing a blend curve with an RPM-dependent authority map. |
| [rusEFI #4057](https://github.com/rusefi/rusefi/issues/4057) | Open | Requests ITB support with SD/Alpha-N switching and weighted model outputs. |
| [rusEFI PR #10089](https://github.com/rusefi/rusefi/pull/10089) / [#10023](https://github.com/rusefi/rusefi/issues/10023) | Open | Separates the second VE map's axis. The discussion explicitly leaves air-model blending to a separate change. |
| [rusEFI PR #9318](https://github.com/rusefi/rusefi/pull/9318) | Closed | Work on blending two VE tables. `merged_at` is null; some commits were picked separately. A closed PR is not evidence of a complete SD/Alpha-N implementation. |
| [rusEFI #8959](https://github.com/rusefi/rusefi/issues/8959) | Open | Discusses larger main VE/ignition maps, rather than specifically enlarging the existing 8 x 8 correction maps. |

Use #539 as a reference, rather than applying it unchanged: independent load
axes, tune conversion, model selection and state publication all need additional
work. Its old file layout also predates this branch's `begin_table` definitions.
In particular, keep legacy VE names, supply the flash-format update, and use
one complete `veAnalyzeMap` record per table. Its single analyzer row containing
three VE map IDs is not a usable multi-map binding. Use separate menus or a
card layout with explicit visibility; a panel's enable condition alone does
not hide the inactive map.
The earlier preparatory commit
[`13d59a619a`](https://github.com/FOME-Tech/fome-fw/commit/13d59a619a)
is also useful: model-specific `getVeImpl()` methods are a better ownership
pattern than switching on global `fuelAlgorithm` inside the common VE helper.

## Original baseline and constraints

This table describes the pre-feature baseline used for the original design;
it is not an inventory of today's implemented branch. Paths are relative to
the repository root.

| Area | Current behavior and relevant source |
| --- | --- |
| Model selection | `firmware/controllers/algo/fuel_math.cpp`: `getAirmassModel()` chooses one global model. Existing enum IDs are SD=0, MAF=1, Alpha-N=2, Lua=3, mock=100. |
| VE lookup | `firmware/controllers/algo/airmass/airmass.cpp`: all three normal models ultimately read `veTable`, `veLoadBins`, `veRpmBins`, with a shared `veOverrideMode`. |
| VE corrections | The same helper applies the idle VE override, then percentage multipliers from `veBlends`, and publishes shared VE state when `postState` is true. |
| SD | `speed_density_airmass.cpp`: air mass uses VE, effective MAP and charge temperature. Effective MAP can be measured, estimated after MAP failure, or selected for a transient. |
| Alpha-N | `alphan_airmass.cpp`: TPS-indexed filling, a fixed 101.325 kPa reference and fixed or measured IAT. This is physically different from SD with a TPS-indexed VE table. |
| MAF | `maf_airmass.cpp`: measured airflow becomes grams/cylinder, corrected by VE; its native load is relative filling before that VE correction. |
| Load publication | `fuel_math.cpp`: `EngineLoadPercent` is published as fueling load and passed to lambda and ignition. Despite its name, it contains MAP kPa, TPS percent or relative filling depending on the model. |
| Other load consumers | Injection phase, cylinder fuel trims, STFT region selection, VVT, lambda monitoring, GPPWM and OBD also consume fueling load. Audit these when adding the mode. |
| Lua | `firmware/controllers/lua/lua_hooks.cpp`: `getAirmass(mode)` evaluates a model with `postState=false`. A diagnostic lookup must not alter live fueling state. |
| Calibration | `firmware/integration/fome_config.txt`: main VE is 16 x 16; existing `blend_table_s` is 8 x 8 and is shared with ignition and boost. |
| Storage | `firmware/controllers/flash_main.cpp`: CRC, version and container size validate the stored tune. Appending fields alone does not preserve a stored binary tune across an update. |

## First-delivery design decisions

These decisions document the first delivery. Apply the linked revision
requirements when planning changes to its controls and load routing.

### 1. Explicit mode and independent maps

Append `LM_SD_ALPHA_N = 4` without renumbering any existing strategy. Keep Lua and
test-only modes unchanged. Select this mode explicitly in TunerStudio.

Retain three independently addressable model instances. Table ownership must
come from the model being evaluated, not the current global `fuelAlgorithm`:
the composite evaluates two models in one cycle, and Lua can request a model
other than the selected strategy.

| Strategy | Main map | Natural load axis |
| --- | --- | --- |
| Speed Density | 16 x 16 VE, existing `veTable` storage | Effective MAP in kPa |
| Alpha-N | New 16 x 16 filling map | TPS percent, with fractional breakpoints |
| MAF | New 16 x 16 correction map | Uncorrected relative cylinder filling, percent |

Each map has its own RPM and load bins. SD and MAF need independent load bins
even though both can use integer storage; Alpha-N needs fractional TPS bins
for useful small-throttle resolution. Use the repository's existing TPS packing
convention rather than inventing a new scale. Keep dimensions named so they can
be varied at build time later; runtime resizing is outside the first delivery.

Legacy load overrides remain effective in legacy mode. Dedicated tables use
their declared natural axes; do not silently apply a shared MAP/TPS override
to all three. TunerStudio must display that distinction and prevent ambiguous
activation with incompatible legacy settings.

### 2. Blend grams per cylinder

Evaluate the two models from one calculation context, with the same RPM and
consistent sensor/configuration inputs:

```text
w = clamp(authority(RPM, TPS) / 100, 0, 1)
m = (1 - w) * mSD + w * mAlphaN
```

The authority table is initially 8 x 8 RPM x TPS, with a 0..100% Alpha-N weight.
At 0%, evaluate only SD for mass; at 100%, evaluate only Alpha-N for mass.
Evaluate both inside the transition. Inputs needed for the authority lookup or
the final load remain required even when one mass branch is inactive.

Stage 1 does not yet provide that shared input context or a raw-model path:
each evaluator still obtains its inputs and applies the existing VE corrections.
Add a compact input capture and separate raw model evaluation from the common
correction pass before composing models. Do not achieve this by changing global
settings temporarily. Preserve standalone evaluation semantics through adapters.

Keep authority evaluation separate from air estimation. RPM/TPS weighting is the
first calibrated policy, not an estimate of sensor confidence. Pressure-relative
ITB policies may follow after qualification of MAP sampling, barometric changes
and high-throttle pressure dips; no universal MAP/baro switch is assumed.

Start with a table containing 0% Alpha-N contribution. Enabling blending must
not implicitly enable an uncalibrated second model. Treat map preparation and
activation as explicit tuning steps.

Do not change either model's pressure/temperature physics in this feature.
Barometric correction, existing temperature corrections, target lambda, enrichment,
wall wetting and injector modeling remain in their existing downstream stages.
Audit the combined path to ensure each correction is applied once.

The existing airflow-based charge-temperature estimate consumes previously
published mass. Publishing composite mass changes that feedback input: include
thermal transients in qualification even though neither model's equation changes.
For boosted applications, qualify the chosen Alpha-N operating region separately;
RPM/TPS alone does not identify different boost pressures at the same operating
point. Additional pressure compensation is a separate calibration/design choice.

### 3. Explicit load sources and one publisher

For the first composite delivery, the default fueling load is **effective SD
MAP in kPa**, independent of the authority weight. This is a compatibility
default, not a requirement that every future composite use MAP. Reuse the SD
MAP result, including any accepted estimate/transient handling; do not read a
different MAP for the same load source. Never interpolate MAP kPa with TPS percent.

Existing AFR and ignition load overrides can still select TPS or normalized
cylinder filling explicitly. Normalized filling is calculated from the final
mass before these overrides are evaluated. Switching from standalone Alpha-N
to composite therefore requires reviewing every default-load calibration, not
only the two main maps.

Resolve composite load sources from the calculation context, with explicit
units, validity and provenance. Today `IFuelComputer::getLoadOverride(AFR_MAP)`
re-reads raw MAP with a numeric failure fallback; preserve that behavior for
legacy callers, but do not silently use it as the composite load contract.
For the composite, default lambda/ignition load uses captured effective MAP;
an explicit `AFR_MAP` override retains its measured-MAP meaning, using the
captured sensor value. TPS, pedal and final-filling overrides retain their
declared sources. Required override inputs must be valid: legacy numeric
fallbacks such as 200 kPa or 100% do not make a composite input valid. A valid
MAP estimate cannot satisfy an explicitly measured-MAP override.

Audit injection phase, cylinder trims, STFT regions, VVT, lambda monitoring,
GPPWM and OBD as well as lambda and ignition. Selecting TPS for lambda and
ignition alone does not remove dependencies through `fuelingLoad` or corrections.
The required revision below replaces a possible global default-load selector
with independent selectors for every consumer. Until implemented and validated,
the current composite retains its MAP default. A sensor is required because an
active model, policy or consumer uses it; never silently change a table's axis
in response to sensor loss.

#### Required revision: independent load selection for every consumer

User requirement confirmed on 2026-09-29; this is planned behavior, not a claim
about the current firmware. Every consumer of composite load must have its own
independent source selection, including effective MAP (kPa) and TPS (percent).
A single global MAP/TPS selector does not meet this requirement.

Audit all direct and indirect uses, including injection phase, each cylinder's
fuel trim, STFT regions, each VVT table, lambda targets, ignition tables and
corrections, staging, lambda monitoring, GPPWM and load-based thresholds or
control logic. Existing selectors can satisfy the requirement where they are
already independent; consumers currently inheriting another function's load
must also be independently configurable. Record every consumer and its routing
before implementing the selectors; the examples above are not an exhaustive list.

Each editor, live cursor, unit label and threshold must use the source selected
for that consumer. Changing one selection must not change another consumer's
source, the model map axes or the blending authority. Migration must preserve
existing effective sources and require deliberate recalibration when the source
changes; relabeling an existing numerical axis is not a calibration conversion.
Sensor dependencies follow the union of active models, policy and consumers.
Changing all downstream selections to TPS does not remove MAP requirements from
an active SD branch or other pressure-dependent calculations. Qualification must
cover mixed selections, independent routing, units and required-input faults.

Separate per-model calculation from final state publication. Keep the numeric
result compact: mass, native load, validity and any additional scalar required
by the composite calculation. Diagnostic capture is optional and caller-owned;
request it when a complete result must be published later without re-running
lookups. Avoid keeping a copy of all correction diagnostics for each branch.
The normal composite calculation evaluates the branches without publication,
applies shared corrections once, and publishes the final state.

The existing standalone `postState=true` wrappers can publish through a small
explicit diagnostic target as values are computed. This preserves their normal
publication order without reserving a full diagnostic capture on the stack.
Public evaluation APIs support no capture or deferred capture only.
`postState=false` must not mutate VE/axis/blend diagnostics or live validity.
Keep numerical equations shared between these delivery paths. Avoid heap
allocation, global scratch buffers and large temporary table copies; the
fast-loop, trigger and Lua callers can run in different contexts.

Publish SD mass, Alpha-N mass, requested and effective authority, final mass,
each table's actual load axis, and validity/fallback status. Define existing
single-VE channels explicitly in composite mode; do not present a weighted
average of the two different VE meanings as a physical VE measurement.

### 4. Idle tables and existing VE corrections

For the first composite release, require `useSeparateVeForIdle=false`. Both
16 x 16 strategy maps cover idle. The existing single idle table cannot safely
be interpreted as both SD efficiency and Alpha-N filling, especially after
importing an Alpha-N tune. Preserve its existing behavior in standalone modes.
Supporting dedicated per-strategy idle tables can be a later, separate change.

Include idle bypass airflow in engine qualification: fixed TPS with a changing
idle-valve opening can change cylinder charge. Neither an idle VE override nor
a second main map alone establishes that airflow. Permit SD- or Alpha-N-dominant
idle as calibrated, and test accessory load, warm-up and return to idle. If the
existing model cannot cover bypass changes, qualify a separate bypass-air model
or correction before claiming support for that operating envelope; avoid counting
the same bypass air both in SD mass and as an additional mass term.

Evaluate existing `veBlends` once in composite mode, using composite MAP load
unless a correction has an explicit Y-axis override. Apply their product once
to the blended mass. Keep their placement and semantics unchanged for legacy
standalone modes. Publish their diagnostics once.

Define the sampling stage of correction inputs as well as their units. Existing
GPPWM fuel/ignition-load channels read previously published state during VE
evaluation. Preserve that timing explicitly where retained; do not publish a
partial SD result merely to feed a correction in the Alpha-N branch. A correction
using final filling would otherwise introduce a circular dependency if that same
correction changes the mass from which filling is calculated.

Endpoint tests must distinguish raw mass from the whole fueling pipeline:
at 0% and 100%, raw composite mass must match the corresponding individual
model with the same table and physics. Final pulse width at 100% need not equal
a standalone Alpha-N tune with TPS-based target lambda or corrections, because
composite mode intentionally retains MAP-based default load. Test common
corrections separately; do not promise unconditional pulse-width equivalence.

### 5. Calibration compatibility and manual restoration

Dedicated maps are now unconditional. Existing SD storage retains its names;
Alpha-N and MAF have separate maps with fixed native axes. Main VE axis override
and manual map-readiness declarations are retired. No automatic conversion
script or binary tune migration is delivered.

Keep an untouched backup of the old TunerStudio project, MSQ and matching INI.
Use them as the source for a manual restore in a separate project with the new
firmware definition. Copy the appropriate map and both axes; compare each field
by meaning, units and representable range. See the
[operator procedure](../user/blended-airmass-it.md).

A native SD/MAP calibration can keep its VE cells if temperature and pressure
policies match. A TPS-indexed legacy SD map cannot be relabeled as MAP: standalone
Alpha-N with Multiply MAP and the same temperature reproduces its mass formula,
but downstream table sources and corrections must also match. Pure Alpha-N,
hybrid Alpha-N and the blended Alpha-N branch require explicit calibration review.
A legacy fixed-temperature Alpha-N map is not automatically equivalent to either
selected measured IAT or estimated Tcharge.

Restore source selectors independently. In particular, staging previously
followed lambda, while ignition trims and knock often followed ignition. Selecting
Model default now is an independent native-model choice. Missing MSQ keys retain
old project values, so a blind legacy import cannot initialize this revision.

The firmware signature and generated page size identify the new layout. Stable
old offsets do not imply binary compatibility. Verify hardware settings, source
choices, maps, axes and corrections; burn, power cycle and save a fresh MSQ for
comparison. Table resizing remains excluded.

### 6. Invalid inputs and activation

Distinguish a legitimate zero mass from an invalid result. Validate monotonic
axes, configured map readiness, mode combinations and finite results before
allowing the new mode to produce injection commands. Runtime sensor validation
also covers required override/authority inputs.

Do not replace an invalid weighted branch with zero and continue on a fraction
of the surviving model. The first release has no automatic promotion to 100%
of a surviving model: that would require a separately qualified fallback
calibration over the entire operating envelope.

When required composite inputs are invalid, publish a specific diagnostic and
latch an injection inhibit, observed directly by `LimpManager::allowInjection()`.
Returning zero air mass is insufficient: fixed cranking fuel, TPS acceleration
enrichment and Lua fuel addition can still request fuel. Only the owning live
fuel calculation can update this gate; `postState=false` cannot.

The initial fault policy prevents **new normal injection sequences after the
fault latch**. Already accepted/queued pulses finish and close normally. Prevent
creating a new split continuation while inhibited, but let any already queued
continuation drain. Do not simply skip an injector-open callback or reset output
counters: a later close can corrupt overlap accounting and close a newer pulse.
This policy has nonzero drain latency; it does not promise instant cancellation
of every queued pulse.

Rearm requires engine stopped, no pending pre-fault fuel callbacks, and an
explicit reset/rearm operation. After rearm, normal scheduling opens only after
a fresh valid positive-RPM fuel calculation and complete per-cylinder fuel state
publication. Fault-free initial startup uses this same fresh-calculation rule
without requiring an operator rearm. Use a distinct not-ready startup state so
RPM=0 does not immediately latch an unrecoverable startup fault. Disallow live
strategy changes that could bypass a latched fault. Preserve legacy fault
behavior outside the new mode.

Disable automatic priming in composite mode at the prime-start callback, including
a prime request queued before a configuration change. Do not prime on recovery.
Prove queued-event drain accounting, split/staged injection, restart and callback
ordering in stage 4. A bound must include every accepted scheduling path; if that
cannot be established, this stage is not complete. This avoids introducing an
unrelated general scheduler cancellation mechanism as a prerequisite.

At a mass endpoint, a failed unused mass branch must not matter. Effective MAP
is still needed at 100% Alpha-N in the first release because its default load
and potentially other consumers use MAP. This dependency may be removed only
for a separately audited explicit load configuration. A MAP estimate is usable
only when its own TPS input and calibration are valid. In particular, do not
silently use zero TPS when both MAP and TPS have failed. Numeric validity,
measured-versus-estimated source, and calibration coverage are distinct; a
finite result is not evidence that a model is accurate at that operating point.

## Resource budget and UI

With 16-bit cells and 16-bit axes, each extra 16 x 16 map costs
`16*16*2 + 16*2 + 16*2 = 576` bytes. Two maps cost 1,152 bytes. An 8 x 8
authority map with 8-bit weights and 16-bit axes costs 96 bytes. The nominal
added calibration payload is **1,248 bytes**, before controls, alignment and
diagnostics. Keep large calibration data outside `engine_configuration_s`
to avoid duplicating it in `activeConfiguration`.

This is a storage estimate, not measured RAM/flash headroom. Record generated
configuration size, live-data size, `.text`, `.data`, `.bss` and stack effects
on Core8 F4 and at least one F7 build. Check 16-bit TunerStudio offsets, flash
configuration boundaries and output-channel packet limits. Measure the fuel
calculation path at endpoints and at 50% blend.

Baseline tracked INIs report a 23,736-byte Core8 page and a 27,736-byte Proteus
page. The nominal payload would make these 24,984 and 28,984 bytes respectively,
before any further fields/alignment. Proteus F7 has a tighter 32 KiB configuration
copy boundary in the supported 2 MiB dual-bank flash layout: include the 12-byte
persistent wrapper when checking that limit. The nominal 28,996-byte container
leaves 3,772 bytes before that boundary.
The existing 1,356-byte output block has only 34 bytes of growth available under
`BLOCKING_FACTOR=1400` and its required 10-byte margin. Budget packed diagnostics
or deliberately update and validate packet sizing; do not add float channels
without checking this constraint.

TunerStudio needs separate SD, Alpha-N, MAF and authority editors, actual axis
labels/units, correct live cursor channels, clear 0%/100% meanings and matching
help text. Show controls for IAT, SD transient handling and barometric correction
in composite mode; the latter already affects running fuel and must stay visible.
Keep the existing 8 x 8 VE/ignition/boost correction structures unchanged.

VE Analyze must not independently tune both models in a mixed region using the
same lambda error. Preserve legacy behavior and add separate, complete analyzer
bindings for dedicated standalone SD, Alpha-N and MAF. Each analyzer must write
the actual active strategy map; the old unconditional `veTableTbl` binding would
otherwise write SD storage while running dedicated Alpha-N or MAF. Until a new
binding is qualified, disable analysis for that dedicated mode. Initially disable
automatic map analysis in composite mode. Qualify endpoint-only tuning later,
with one active model, stable authority and understood downstream corrections.

## Original development sequence

For the next iteration, use the [revision sequence](blended-airmass-revision.md#development-and-acceptance).

Keep each change buildable and reviewable, following `CONTRIBUTING.md`.
Use one integration branch initially; split PRs only at independently useful,
tested boundaries. Do not publish a runtime mode before its configuration,
diagnostics and failure handling are complete.

| Stage | Work | Lead / review | Exit evidence |
| --- | --- | --- | --- |
| 0 | Record baseline, map call paths, finalize this design and existing-test gaps. | Astra high; Sol high for build/config inventory | Exact base SHA, baseline results and known limitations. |
| 1 | Separate model table selection and calculation diagnostics from publication without changing existing behavior. | Astra high implementation; Sol high source review; Astra high test review | Regression tests for SD, Alpha-N, MAF, overrides, idle VE, corrections and Lua dry reads. |
| 2 | Add independent tables/axes, defaults, explicit opt-in and import/conversion support. Add standalone editors and analyzer bindings. | Sol high; Astra high reviews compatibility | Old MSQ fixtures retain behavior; dedicated maps use their own axes; each analyzer writes only its active map; generated layout/INI checks and resource delta. |
| 2a | Add shared input capture, raw model/correction separation and explicit composite load resolution. | Astra high; Sol high audits consumers/tests | Legacy parity; documented units, sources and dependencies; no repeated sensor reads for the same composite source. |
| 3 | Implement composite calculation, initial MAP default load, correction placement and validated authority. | Astra high | Endpoint/intermediate mass tests, unchanged model physics, one publisher and one correction pass. |
| 4 | Complete fault gating/recovery, mode activation, diagnostics, TS editors and VE Analyze policy. | Astra high for runtime gating; Sol high for UI/tests | Fault latch prevents new injection despite cranking/AE/Lua requests; queued pulses drain correctly; correct live cursors and no stale mixed-mode state. |
| 5 | Full host tests, ARM builds, resource/timing comparison, simulator and TunerStudio checks. | Sol high; independent Astra high review | Passing required checks, documented board sizes/timing and reproducible calibration workflow. |
| 6 | Bench qualification, then separately arranged engine calibration. | Developer and operator | Recorded endpoint calibration and transition logs; hardware/engine results explicitly distinguished from automated tests. |

For parallel work, give each editing agent distinct files or a separate
worktree. Configuration generation, host tests and firmware builds in the same
worktree must run sequentially: they share generated headers and PCH files.
Have the reviewer check final diffs and test evidence, not only summaries.

## Required tests

- Legacy strategy enum values, table names, defaults and standalone results.
- Independent SD/MAP, Alpha-N/TPS and MAF/filling axes, including fractional TPS,
  different RPM bins, interpolation boundaries and non-square test data where
  useful to detect reversed table axes.
- Analytical 0%, 25%, 50%, 75% and 100% mass results; clamping at map boundaries;
  correct pressure/temperature response in each branch.
- Effective MAP consistency between SD mass and composite load, including
  validated MAP estimation and transient handling.
- Shared correction product applied once; explicit correction-axis overrides;
  unsupported idle-table configuration rejected for composite mode.
- One coherent state publication; Lua dry reads and inactive branch evaluation
  do not overwrite current VE, load, authority or injection-validity state.
- Final normalized filling and existing AFR/ignition overrides; downstream
  load consumers never receive a weighted mixture of units.
- All five load selections for lambda and ignition independently, including
  accelerator pedal and measured MAP distinct from effective MAP. Staging uses
  the lambda-table axis, while lambda monitoring and other default-load consumers
  use `fuelingLoad`; test their routing separately. Preserve MAF's pre-correction
  native load versus its post-correction normalized filling in standalone mode.
- Shared input capture, measured/effective MAP provenance, required-input
  dependencies for every supported load configuration, and unchanged legacy
  override fallbacks. A missing inactive model input differs from a missing
  input still required by a downstream table.
- Required sensor loss, nonfinite result, invalid authority/axes, unused branch
  failure, startup before a valid sample, recovery and runtime configuration
  changes. Test actual scheduled injector behavior, not only returned mass:
  latch blocks new sequences, accepted pulses drain, no new split continuation,
  overlap counters remain correct, priming is suppressed and rearm cannot race
  old callbacks. Measure the maximum accepted-pulse drain latency.
- Legacy SD, Alpha-N and MAF tune conversion; repeated imports; pre-existing
  dedicated settings; save/reload/burn; incorrect signature and version paths.
- TunerStudio labels, live cursors, visibility, authority units, analysis
  enablement and correct destination through strategy changes, visible barometric
  corrections, and generated configuration/output size limits.

Engine qualification follows the comparison matrix in
[Air estimation and load architecture](airmass-architecture.md#qualification).
Include idle bypass changes, a pressure dip at high TPS, warm-up/heat soak,
barometric variation and model disagreement in the overlap. Host tests and
synthetic log replay do not establish engine accuracy or transient performance.

## Validation commands and completion record

Follow the repository's `CLAUDE.md` and the installed `fome-build` skill.
Generators run through normal builds; do not commit generated outputs.

```bash
# Run from the dedicated worktree's unit_tests directory.
make -j12
# During development, select relevant tests before the integration run.
./build/fome_test --gtest_filter='AirmassEvaluation.*:AirmassModes.*:FuelMath.*:LuaHooks.*'
./build/fome_test

# After an appropriate clean, run sequentially from each board directory.
cd ../firmware/config/boards/core8
bash compile_core8.sh
cd ../proteus
bash compile_proteus_f7.sh
```

Use focused tests during development, then the full suite at integration gates.
Once configuration changes are ready, generate all board definitions with the
checked-in `firmware/gen_config.sh` workflow and inspect each board's layout.
Compare every pre-existing constant's generated offset, type and shape to the
baseline, allowing only deliberate documented changes; check old enum IDs and
VE names separately. A normal build generates only its selected board.
If host/ARM builds share a checkout, follow the documented PCH cleanup process.
Record command, revision, exit code and logs for every claimed result.

### Initial results (2026-09-27)

Firmware source revision: `ede26b3702126e86fc956543e8ff0e9d4a986aa6`, on the
dedicated branch. Only this design document was added during the investigation.

| Check | Result |
| --- | --- |
| Host build, `unit_tests/`: `make -j12` | Exit 0. Generated configuration and compiled `build/fome_test`. Existing compiler warnings remain. |
| Full suite, `unit_tests/`: `./build/fome_test` | Exit 0; **720 tests passed, 135 test suites**, 2.875 seconds reported by GoogleTest. |
| Independent design review | Astra high reviewed the plan against source; dedicated-mode analyzer bindings and composite barometric-editor visibility were added following the review. Sol high verified configuration layout, resource calculations and test/UI requirements. |

The final independent Astra high pass found no material contradictions in the
revised design. Fault-drain accounting and timing remain implementation gates,
not completed verification.

Local evidence:

- `/tmp/fome-blended-airmass-baseline-build.log`
- `/tmp/fome-blended-airmass-baseline-tests.log`
- `/tmp/fome-blended-airmass-dependencies.log`

The initial automatic dependency download was stopped before compilation to
reuse existing local repositories as Git object references. All submodules were
then checked out at this revision's pinned commits; the successful build and
test run above followed that initialization. The only tracked build output
change was the generated f407-discovery INI branch/date signature; it is excluded
from the design commit and restored to its original tracked content.

These initial results preceded firmware implementation. Stage 1 results follow.

### Stage 1: compact evaluation and optional diagnostics

SD, Alpha-N and MAF expose evaluation APIs that return mass, native load and
numeric validity without publishing live diagnostics. A caller can optionally
supply an `AirmassDiagnostics` buffer, retain its values across other model
queries, and publish it explicitly. Existing warning codes remain enabled.

The legacy `getAirmass(..., postState)` and `getVe(..., postState)` wrappers use
the same calculation functions. With `postState=true`, an explicit diagnostic
target writes each correction result as it is calculated, followed by final VE
and axes. It does not allocate a full capture. With `postState=false`, neither
capture nor publication is requested. This preserves numeric fallbacks,
correction multiplication order and partial publication on failed SD calls.
Publication consists of sequential field writes; it is not an atomic snapshot.

The ARM layouts are:

| Type | Bytes | Contents |
| --- | ---: | --- |
| `AirmassEvaluation` | 12 | Mass, native load, numeric validity. |
| `VeEvaluation` | 8 | Corrected VE and numeric validity. |
| `AirmassDiagnostics` | 92, optional | VE, axes, four correction results, MAP selection and presence/validity flags. |
| `VeDiagnostics` | 80, optional | VE portion of the full capture. |

The previous implementation (`ea1da36cdf`) returned a 104-byte snapshot on every
model call. The compact API moves its 92-byte diagnostic payload into an
optional caller-owned buffer. Requesting both still needs 104 bytes of payload;
ordinary model calls no longer pay for that buffer. Actual stack use also
includes compiler temporaries, saved registers and nested calls, so the type
sizes alone do not measure the stack saving.

Reusing a capture clears its presence and validity flags at entry. Absent
diagnostic payloads are not guaranteed to be refreshed and must not be
published. For example, an explicit SD MAP is retained numerically, while
`Map.HasValue` stays false because no fallback MAP was evaluated for publication.
Publication checks presence independently of numeric validity, preserving
legacy failure behavior without republishing a previous call's values. No heap allocation,
global scratch buffer or diagnostic-array copy is introduced in the normal
calculation path.

Each model has its own VE lookup override. All three still use the legacy VE
table and axes in this stage; calibration layout, mode IDs, defaults and
TunerStudio controls remain unchanged. Lua's existing `getAirmass` hook is
compiled into host tests and remains a query without live diagnostic writes.

Numeric validity distinguishes unusable model inputs/results from a legitimate
zero mass. It does not validate calibration readiness or provide injection
fault gating. Unavailable correction-channel inputs still use the existing
neutral correction; the composite implementation must validate them separately.

Astra high implemented the compact API and reviewed the regressions. Sol high
wrote the capture/no-capture parity and buffer-reuse tests, independently
reviewed the firmware, and inspected ARM resource use.

#### Automated validation (2026-09-27)

These results use the compact evaluation changes on parent commit
`ea1da36cdf74c7dd3654c325ffe6ca7429e72628`. ARM baseline builds used a separate
worktree at `ede26b3702126e86fc956543e8ff0e9d4a986aa6`, with the same pinned
toolchain and board scripts. Host and ARM builds ran sequentially, with explicit
firmware and bootloader cleans and host PCH removal between targets.

| Check | Result |
| --- | --- |
| Host `make -j12` | Exit 0; existing compiler warnings remain. |
| Targeted `AirmassEvaluation.*:AirmassModes.*:FuelMath.*:LuaHooks.*` | Exit 0; 42 tests passed. |
| Full `./build/fome_test` from `unit_tests/` | Exit 0; **733 tests passed in 136 suites**. |
| Core8 `bash compile_core8.sh` | Exit 0, bootloader and main firmware. |
| Proteus F7 `bash compile_proteus_f7.sh` | Exit 0, bootloader and main firmware. |
| Vendored clang-format dry run and `git diff --check` | Exit 0 for changed C++ files and documentation. |

#### Stack allocation and compiled memory use

MainLoop requests 1,088 bytes of application stack, compared with 1,024 upstream
and 1,280 in `ea1da36cdf`. The added reserve is **64 bytes instead of 256**. The
largest increase over upstream on the selected normal airmass call paths is
48 bytes, so this allocation preserves their previous arithmetic cushion.

ARM disassembly confirms that ordinary model calls use a 12-byte diagnostic
target and no full diagnostic buffer. No model or shared evaluator contains
the previous 104-byte initialization or diagnostic-array copy. Public optional
capture APIs have no production caller yet and are eliminated by LTO; their
independent stack frames therefore cannot be measured from these firmware ELFs.
Host regressions cover their behavior.

Selected call-path sums include `ThreadTask`, `MainLoop::PeriodicTask`,
`Engine::periodicFastCallback`, the model, VE correction, GPPWM input lookup,
and either a stored sensor with timeout check or a simple redundant-to-stored
sensor chain:

| SD path | Upstream | Previous snapshot | Compact result |
| --- | ---: | ---: | ---: |
| Core8, stored sensor | 724 | 852 | 772 |
| Core8, redundant sensor | 780 | 908 | 828 |
| Proteus F7, stored sensor | 708 | 852 | 748 |
| Proteus F7, redundant sensor | 764 | 908 | 804 |

These are selected static lower bounds, not worst-case stack requirements or
measured free margins. Indirect sensor composition, exceptional paths and
runtime history remain outside that calculation. ChibiOS adds bookkeeping,
context and interrupt reserves around the requested stack; those reserves
are not additional application call-frame budget.

Final measured sizes are bytes. GNU `size` text includes read-only data;
branch/date signature strings also differ between upstream and this branch.

| Board | GNU text, upstream -> compact | Data | GNU BSS | Config page | Output block |
| --- | --- | --- | --- | --- | --- |
| Core8 | 651,602 -> 651,926 (**+324**) | 2,504, unchanged | 186,816 -> 186,880 (**+64**) | 23,736, unchanged | 1,356, unchanged |
| Proteus F7 | 691,302 -> 691,658 (**+356**) | 1,332, unchanged | 458,220, unchanged | 27,736, unchanged | 1,356, unchanged |

Core8 `.ram4` is 58,320 bytes, leaving 7,216 bytes of CCM; its 8,680-byte heap is
unchanged. Proteus F7 `.ram3` is 64,880 bytes, with a 66,192-byte remaining heap.
F7's aggregate GNU BSS includes this heap, so its unchanged total hides the
64-byte transfer from heap to static allocation. Both boards recover **192
bytes of static RAM** compared with `ea1da36cdf`.

The `.text` section grows by 300 bytes on Core8 and 320 on F7 relative to
upstream; `.rodata` grows by 24 and 36 bytes. Compared with `ea1da36cdf`, GNU
text decreases by 260 and 368 bytes respectively. Bootloader S-records match
upstream byte for byte. Final deliverables:

| Board | `fome.bin` | `fome_update.srec` | `fome_bl.srec` |
| --- | ---: | ---: | ---: |
| Core8 | 687,200 | 1,963,386 | 60,460 |
| Proteus F7 | 725,768 | 2,079,098 | 59,692 |

On-target MainLoop stack watermarks and execution-time measurements remain
required before engine use and before adding simultaneous model evaluation.
Check `threads` after representative sensor/correction and fault paths, and
compare `PE::GetBaseFuel`, `PE::GetSpeedDensityFuel` and MainLoop timing. Rare
trigger/RPM-transition calls use the separate exception stack and retain their
own watermark check. No ECU timing or watermark was measured in this stage.

Matching generated INIs, ELF/map files, images, hashes, host logs and the stack
review are retained locally:

- `/tmp/fome-blended-compact-artifacts/{core8,proteus-f7}/`
- `/tmp/fome-blended-compact-artifacts/host-{build,targeted,tests}.log`
- `/tmp/fome-blended-compact-artifacts/stack-review.md`
- `/tmp/fome-blended-compact-1280-artifacts/stack-review.md` (initial compact build)
- `/tmp/fome-blended-stage1-artifacts/` (previous snapshot implementation)
- `/tmp/fome-blended-baseline-artifacts/` (upstream)

Generated INI signature and connector-path changes are excluded from the
commit after inspection. The next stage adds independent 16 x 16 tables and
axes, explicit opt-in and conversion, followed by the composite calculation
and injection fault policy. Simulator, TunerStudio and hardware qualification
of the new mode remain future gates.

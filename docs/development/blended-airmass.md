# Blended Speed Density and Alpha-N development plan

## Status and scope

Design started on 2026-09-27. Stage 1 separates model evaluation from diagnostic
publication. Independent calibration maps and the SD + Alpha-N mode remain
planned work; the new mode is not yet available in firmware or TunerStudio.

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

## Current code and constraints

Paths in this section are relative to the repository root.

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

## Design decisions

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

### 3. One final load and one publisher

In composite mode, the default fueling load is **effective SD MAP in kPa**,
independent of the authority weight. Reuse the SD MAP result, including any
accepted estimate/transient handling; do not read a different MAP for the final
load. Never interpolate MAP kPa with TPS percent.

Existing AFR and ignition load overrides can still select TPS or normalized
cylinder filling explicitly. Normalized filling is calculated from the final
mass before these overrides are evaluated. Switching from standalone Alpha-N
to composite therefore requires reviewing every default-load calibration, not
only the two main maps.

Separate per-model calculation from final state publication. A model result
needs mass, native load, VE/filling, validity, and enough diagnostics to publish
without re-running the lookup. The normal composite calculation publishes once.
`postState=false` must not mutate VE/axis/blend diagnostics or live validity.
Avoid heap allocation and large temporary table copies.

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

Evaluate existing `veBlends` once in composite mode, using composite MAP load
unless a correction has an explicit Y-axis override. Apply their product once
to the blended mass. Keep their placement and semantics unchanged for legacy
standalone modes. Publish their diagnostics once.

Endpoint tests must distinguish raw mass from the whole fueling pipeline:
at 0% and 100%, raw composite mass must match the corresponding individual
model with the same table and physics. Final pulse width at 100% need not equal
a standalone Alpha-N tune with TPS-based target lambda or corrections, because
composite mode intentionally retains MAP-based default load. Test common
corrections separately; do not promise unconditional pulse-width equivalence.

### 5. Calibration compatibility and conversion

Keep existing `veTable`, `veLoadBins` and `veRpmBins` names and storage intact.
Append the Alpha-N and MAF tables, their independent axes, authority table and
new controls without shifting old fields wherever possible. Check generated
offsets rather than assuming that appending inside a nested struct is harmless.

Add an explicit opt-in for dedicated strategy tables, defaulting to disabled:

- Disabled: all existing standalone modes use the original shared table and
  overrides, preserving imported legacy tune behavior.
- Enabled: SD uses existing storage, Alpha-N and MAF use their dedicated maps.
- Composite mode requires dedicated tables and a valid supported configuration.

The unused bit `unused580b5` is a candidate for the opt-in control, preserving
the engine configuration size. Verify its generated offset and all consumers
before repurposing it. The existing three-bit `fuelAlgorithm` field can encode
the new value without widening the field.

Audit engine presets and default-map helpers as part of this step. Presets such
as BMW M73 and the Miata variants write the legacy VE fields directly; keep their
default behavior intact, and initialize new tables without claiming that a flat
default is a calibrated alternative model.

Conversion is an explicit stopped-engine operation. From a legacy Alpha-N or
MAF tune, copy the original map and both axes to that strategy's dedicated map
before reusing the original storage for a calibrated SD map. From a legacy SD
tune, retain its table and prepare Alpha-N separately. Never convert MAP bins
to TPS by relabeling them or assume the same cell values calibrate both models.

Do not infer an old MSQ from missing new keys: importing into a project can
leave previous values for absent fields. Provide a reproducible conversion
procedure/tool with legacy SD, Alpha-N and MAF fixtures, preserving the original
MSQ. Require disabled blending/dedicated-table mode before legacy import unless
the conversion tool explicitly supplies the new settings.

Binary flash migration is not part of the first implementation. A format
change must update the required version/signature and release notes. Record
that firmware update requires tune backup and validated restore/conversion.
Append-only layout is useful for old field names and offsets, but is not a
claim of in-place binary compatibility.

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
is still needed for the final composite load at 100% Alpha-N. A MAP estimate is
usable only when its own TPS input and calibration are valid. In particular,
do not silently use zero TPS when both MAP and TPS have failed.

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

## Development sequence

Keep each change buildable and reviewable, following `CONTRIBUTING.md`.
Use one integration branch initially; split PRs only at independently useful,
tested boundaries. Do not publish a runtime mode before its configuration,
diagnostics and failure handling are complete.

| Stage | Work | Lead / review | Exit evidence |
| --- | --- | --- | --- |
| 0 | Record baseline, map call paths, finalize this design and existing-test gaps. | Astra high; Sol high for build/config inventory | Exact base SHA, baseline results and known limitations. |
| 1 | Separate model table selection and calculation diagnostics from publication without changing existing behavior. | Astra high implementation; Sol high source review; Astra high test review | Regression tests for SD, Alpha-N, MAF, overrides, idle VE, corrections and Lua dry reads. |
| 2 | Add independent tables/axes, defaults, explicit opt-in and import/conversion support. Add standalone editors and analyzer bindings. | Sol high; Astra high reviews compatibility | Old MSQ fixtures retain behavior; dedicated maps use their own axes; each analyzer writes only its active map; generated layout/INI checks and resource delta. |
| 3 | Implement composite calculation, fixed final load, correction placement and validated authority. | Astra high | Endpoint/intermediate mass tests, unchanged model physics, one publisher and one correction pass. |
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

### Stage 1: model evaluation and publication

SD, Alpha-N and MAF now return calculation snapshots containing mass, native
load, VE, correction diagnostics and, for SD, MAP selection. Callers can evaluate
several models and explicitly publish one captured result. The existing
`getAirmass(..., postState)` and `getVe(..., postState)` APIs wrap this operation,
including legacy numeric fallbacks and partial diagnostic publication on SD
failures. Evaluation still emits existing warning codes.

The VE helper fills each model's snapshot directly. The initial ARM builds
exposed an extra 80-byte VE temporary and copy, which increased stack use on the
1,024-byte MainLoop thread. The in-place helper removes that temporary while
keeping initialized snapshots on early failure paths.

Each model has its own VE lookup override. All three still use the legacy VE
table and axes in this stage; no calibration layout, mode IDs, defaults or
TunerStudio controls change. Lua still requests evaluation without publication.
Its existing read-only `getAirmass` hook is now also compiled into host tests;
the regression runs actual Lua queries for all three models.

Snapshot validity distinguishes unusable model inputs/results from a legitimate
zero mass. It does not validate calibration readiness or supply injection fault
gating. In particular, unavailable correction-channel inputs still use the
existing neutral correction. Their validation is required before enabling the
composite mode.

Astra high implemented the model changes. Sol high independently reviewed the
firmware diff and wrote the regressions; Astra high reviewed those tests. The
review added cross-model snapshot isolation, zero-mass validity, failed SD
publication and MAP-estimate cases. No source behavior regression was identified.

#### Automated validation (2026-09-27)

These results use the final stage 1 source changes on parent commit
`d38184f36478c5ef22f799b47acd6f2f65886711`. ARM baseline builds used a separate,
clean worktree at `ede26b3702126e86fc956543e8ff0e9d4a986aa6` with the same pinned
toolchain and board scripts. Host and ARM builds ran sequentially in the feature
worktree, with board-specific clean targets and host PCH removal between them.

| Check | Result |
| --- | --- |
| Host `make -j12` | Exit 0; existing compiler warnings remain. |
| Targeted `AirmassEvaluation.*:AirmassModes.*:FuelMath.*:LuaHooks.*` | Exit 0; 39 tests passed. |
| Full `./build/fome_test` from `unit_tests/` | Exit 0; **730 tests passed in 136 suites**, 3.084 seconds. |
| Core8 `bash compile_core8.sh` from its board directory | Exit 0, bootloader and main firmware. |
| Proteus F7 `bash compile_proteus_f7.sh` from its board directory | Exit 0, bootloader and main firmware. |
| Vendored clang-format dry run and `git diff --check` | Exit 0 for all changed C++ files and the new test. |

Sizes are bytes. GNU `size` text includes read-only data; it is not the `.text`
section alone. Signature strings also differ between baseline and feature builds.

| Board | GNU text, baseline -> stage 1 | Data | BSS | Config page | Output block |
| --- | --- | --- | --- | --- | --- |
| Core8 | 651,602 -> 652,186 (**+584**) | 2,504, unchanged | 186,816 -> 187,072 (**+256**) | 23,736, unchanged | 1,356, unchanged |
| Proteus F7 | 691,302 -> 692,026 (**+724**) | 1,332, unchanged | 458,220, unchanged | 27,736, unchanged | 1,356, unchanged |

Both boards reserve 256 additional static bytes for MainLoop. On Core8, `.ram4`
grows from 58,256 to 58,512 bytes, leaving 7,024 bytes of CCM; the 8,680-byte heap
is unchanged. On Proteus F7, `.ram3` grows from 64,816 to 65,072 bytes and its
remaining heap decreases from 66,256 to 66,000 bytes. GNU `size` includes that
heap in its BSS total, so the unchanged aggregate on F7 does not mean unchanged
static allocation.

The `.text` section grows by 560 bytes on Core8 and 688 bytes on Proteus F7;
`.rodata` grows by 24 and 36 bytes respectively. Both bootloader S-records have
the same SHA-256 hashes as baseline. Core8 deliverables are `fome.bin` (687,460
bytes), `fome_update.srec` (1,964,178) and `fome_bl.srec` (60,460). Proteus F7
deliverables are 726,136, 2,080,202 and 59,692 bytes respectively.

ARM disassembly confirms that the in-place helper removes 80 bytes from each
model's stack frame compared with the first implementation. Only the 104-byte
model snapshot initialization remains; the additional 80-byte initialization
and copy are gone. Final function frame sizes, compared with upstream, are:

| Board | Alpha-N / MAF frame | SD frame | Shared VE calculation frame |
| --- | --- | --- | --- |
| Core8 | 64 -> 160 bytes | 56 -> 192 bytes | 96 -> 88 bytes |
| Proteus F7 | 64 -> 160 bytes | 56 -> 200 bytes | 88 -> 88 bytes |

MainLoop's requested thread stack increases from 1,024 to 1,280 bytes to offset
the additional model snapshot. ChibiOS separately adds working area space for
thread bookkeeping, context and interrupt reserves; those reserves are not
extra application stack budget. Including the periodic wrapper, MainLoop,
engine callback, model, VE helper, enabled correction and a stored sensor with
its timeout check gives a selected call-path lower bound of 852 bytes for SD on
both boards. A simple redundant-sensor chain raises that to 908 bytes. This
motivated the extra 256-byte allocation. These values are not a worst-case bound
or a measured remaining margin; indirect sensor and fault paths depend on
configuration.

The final code still uses more stack than upstream. On-target MainLoop stack
watermarks and execution-time measurements remain required before engine use
and before adding simultaneous model evaluation. No timing or stack watermark
was measured on an ECU during this stage.

An incremental Proteus F7 rerun encountered an incompatible firmware PCH while
building the bootloader. The final successful builds used explicit firmware
and bootloader cleans; no compiler flags or source were changed to bypass it.

Build-generated tracked changes were confined to INI branch/date signatures
and connector source-path comments; they were restored after inspection.
Matching generated INIs, ELF/map files, firmware images and hashes are retained
with the local evidence:

- `/tmp/fome-blended-stage1-final-build.log`
- `/tmp/fome-blended-stage1-targeted.log`
- `/tmp/fome-blended-stage1-tests.log`
- `/tmp/fome-blended-stage1-core8.log`
- `/tmp/fome-blended-stage1-proteus-f7.log`
- `/tmp/fome-blended-stage1-artifacts/{core8,proteus-f7}/`
- `/tmp/fome-blended-stage1-artifacts/stack-review.md`
- `/tmp/fome-blended-baseline-artifacts/{core8,proteus-f7}/`

The next implementation stage adds independent 16 x 16 tables and axes,
explicit opt-in and conversion, followed by the composite calculation and
injection fault policy. Simulator, TunerStudio and hardware qualification of
the new mode remain future gates.

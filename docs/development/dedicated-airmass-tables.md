# Stage 2: independent standalone airmass tables

This is the record of the implemented stage 2 behavior. The
[next revision](blended-airmass-revision.md) removes the dedicated-map opt-in
and defines the required migration; those changes are not implemented yet.

Guida operativa completa in italiano: [controlli, mappe e procedure](../user/blended-airmass-it.md).

Implemented after the compact evaluation refactor (`642704d219`), on
`feature/blended-airmass`. This stage provides independent calibration for the
three existing strategies. Shared input capture and SD/Alpha-N mass blending
follow in later stages.

## Configuration and behavior

`useDedicatedAirmassTables` defaults to false. With it disabled, all existing
strategies retain the shared VE table, load overrides, idle table and corrections.
Engine presets still populate the same legacy fields.

With it enabled, main maps use their natural coordinates:

| Model | Map | Load axis | RPM axis |
| --- | --- | --- | --- |
| SD | `veTable` | `veLoadBins`, effective MAP in kPa | `veRpmBins` |
| Alpha-N | `alphaNTable` | `alphaNTpsBins`, TPS percent at 0.01% storage resolution | `alphaNRpmBins` |
| MAF | `mafTable` | `mafLoadBins`, uncorrected relative cylinder filling in percent | `mafRpmBins` |

Each map is 16 x 16 with its own axes. Cells retain the existing 0.1% encoding.
Map ownership follows the model being evaluated, including a query for a model
other than the configured fuel strategy. Existing pressure/temperature equations,
native load publication, idle overrides and VE correction multiplication are
preserved. The main VE override must be `None` in dedicated mode.

New maps have placeholder defaults: Alpha-N 80%, MAF 100%, and independent axes.
The Alpha-N TPS bins include fractional breakpoints near closed throttle. These
defaults require engine calibration before selecting the corresponding strategy.

The dedicated control reuses offset 580 bit 5 (`unused580b5`). New maps and axes
are appended to `persistent_config_s`, outside `engine_configuration_s`, adding
1,152 bytes to the configuration page. Existing field offsets and mode IDs stay
unchanged. Flash format version advances from 20024 to 20025; stored binary tunes
are not migrated in place.

## Validation and fault ownership

When dedicated tables are enabled, both new maps' axes must be strictly
increasing. TPS is limited to 100%, RPM to 18,000, and MAF load to 1,000%.
The packed unsigned storage already excludes negative and nonfinite values.

The pure validator checks compatibility before interpolation. Dry model and Lua
queries can return an invalid result without changing published diagnostics or
fault state. Boot, configuration-change/burn handling and the live fuel owner
use the same validation with FOME's existing fatal configuration-error handling.
This also catches invalid live edits and avoids relying on zero mass alone to
prevent fuel additions elsewhere in the pipeline.

Validation does not cache live-writable axes. With dedicated tables enabled,
the normal fuel path scans the four axes in both owner validation and model
evaluation: 120 adjacent comparisons plus range checks. With the option disabled,
it returns before scanning. Calibration quality is a separate operator check;
ascending axes do not establish that an engine is correctly calibrated.

## TunerStudio

Standalone editors and VE Analyze bindings select the appropriate map. The
shared editor's axis label follows the legacy strategy/override; dedicated
editors label their natural units. The live cursor uses the published main VE
lookup coordinate. Both menu conditions and panel visibility follow the active
strategy, so an already-open inactive panel stops displaying another model's
cursor after a strategy change.

All dedicated VE Analyze bindings are disabled while `useSeparateVeForIdle` is
configured. The shared idle table can contribute during idle and taper; tuning
the main table alone against that contribution would adjust the wrong map.
The legacy analyzer condition is preserved. This restriction affects analysis,
not standalone idle-table operation.

The dedicated control is editable in TS with the engine stopped and the VE
override set to `None`. It can still be disabled to recover an incompatible
import. Prepare/convert the selected map before enabling it.

## Manual tune restoration (current revision)

The first delivery included a converter. It has been removed at the owner's
request. Dedicated maps are now always active, and the old opt-in and readiness
flags are retired. Use an untouched TunerStudio backup as the source for manually
copying controls, cells and both axes into a separate matching project.
See [the current operator guide](../user/blended-airmass-it.md) and
[revision implementation record](blended-airmass-implementation.md).

The other sections in this file record stage 2 and its measured results; they
are historical evidence, not the current activation or temperature contract.

## Automated validation

Host commands run from `unit_tests/`:

```sh
make -j12
./build/fome_test --gtest_filter='DedicatedAirmassTables.*:AirmassEvaluation.*:AirmassModes.*:FuelMath.*:LuaHooks.*'
./build/fome_test
```

Results: **742 tests passed in 137 suites**, including 51 targeted tests.
Coverage includes default/preset compatibility, independent interpolation with
fractional TPS, requested-model ownership, native mass/load, idle taper,
legacy overrides, dry invalid queries and live/configuration rejection.

The converter's 12 Python tests pass. Integration checks accept all 28 generated
board INIs and perform 84 synthetic SD/Alpha-N/MAF conversions. A pre-existing
Core8 Alpha-N/TPS backup also passed an in-memory conversion/preservation check;
no private calibration is included in the committed fixtures.

All board definitions are regenerated using `bash gen_config.sh` from
`firmware/`. Generated files are inspected and archived, then excluded from the
source commit. TunerStudio interactive import, burn/reload, panel behavior and
VE Analyze operation require separate application/ECU checks; generated INI
validation does not establish those runtime results.

## ARM resources

Clean parent and feature builds used the pinned ARM GCC 11.3 toolchain and the
Core8/Proteus F7 board scripts, including bootloaders. Each board build was
preceded by firmware and bootloader cleans; host PCH files were removed before
ARM compilation. All commands completed successfully.

| Metric | Core8, phase 1 -> phase 2 | Proteus F7, phase 1 -> phase 2 |
| --- | ---: | ---: |
| GNU text, including read-only data | 651,902 -> 653,246 (+1,344) | 691,622 -> 694,818 (+3,196) |
| Initialized data | 2,504 -> 2,504 | 1,332 -> 1,332 |
| Static CCM/DTCM allocation | 58,320 -> 59,472 (+1,152) | 64,880 -> 66,032 (+1,152) |
| Linker heap | 8,680 -> 8,680 | 66,192 -> 65,040 (-1,152) |
| Configuration page | 23,736 -> 24,888 | 27,736 -> 28,888 |
| Output packet | 1,356 -> 1,356 | 1,356 -> 1,356 |

New calibration storage is reserved even when the dedicated option is disabled.
The option selects behavior; it does not remove the tables from the build.
Core8 has 6,064 bytes of unallocated CCM after this change. F7's aggregate GNU
BSS remains 458,220 because its linker heap shrinks as static allocation grows;
that unchanged total does not mean unchanged RAM use.

Parent builds used detached HEAD, so branch signature strings account for
24 bytes of the Core8 GNU text comparison and 36 bytes on F7. The `.text`
section itself grows by 992 and 1,408 bytes respectively. Read-only data also
includes diagnostics/defaults and, where enabled, the embedded INI. Both
bootloader S-records are byte-identical to their parent builds.

MainLoop still requests **1,088 bytes** of application stack. Selected SD paths
through a stored sensor remain 772 bytes on Core8 and 748 on F7; a simple
redundant-sensor chain gives 828/804 bytes. Other inspected model paths retain
or reduce their frame totals. The new successful owner-validation paths use
416/400 bytes; inspected fatal-message/version formatting paths use 728/708.
These comparisons do not indicate a need to enlarge the reserve for stage 2.
They are selected static path sums, not a worst-case bound or measured free
stack. ECU stack watermarks and timing, including enabled validation and error
handling, remain to be measured.

All 28 generated variants retain existing parameter layouts, except renaming
the reserved control bit. Each has exactly 1,152 appended bytes. Output size
remains 1,356 with a blocking factor of 1,400, leaving 44 bytes total or 34 after
the required 10-byte margin. Proteus F7's 28,888-byte page plus the 12-byte
persistent wrapper is 28,900 bytes, leaving 3,868 below its 32 KiB copy boundary.

Artifacts, matching INIs, parent builds, hashes and audit scripts are retained in
`/home/deffie/fome-artifacts/blended-airmass/stage2-2026-09-28/`. These include
`layout-audit-report.md`, `conversion-integration.txt`, `stage2-arm-review.md`, host/Python test logs,
and the `parent/` and `feature/` firmware directories.

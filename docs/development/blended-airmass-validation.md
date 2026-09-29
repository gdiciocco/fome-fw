# Blended airmass validation

Software and ARM review recorded on 2026-09-29 for
`feature/blended-airmass`, final build source
`7170c8f175c83a6a0e52197f8aa981c5fecf1800`. Runtime tests used firmware
`68632b305be899fd18285c6d7e582224ddb9336b`; the only subsequent change encodes
the TunerStudio rearm payload. That patched INI was tested against 68632.
Both final 7170 ARM artifacts were built and reviewed; they were not reflashed
for another runtime run.
See [operation](blended-airmass-operation.md) for calibration and recovery rules
and [input contracts](airmass-input-contracts.md) for model boundaries.

## Evidence and scope

Local evidence is archived under:

- `/home/deffie/fome-artifacts/blended-airmass/stage2-2026-09-28/`
- `/home/deffie/fome-artifacts/blended-airmass/stage3-2026-09-29/`

The stage 3 archive contains test logs, generated-layout and conversion audits,
ELF/map files, deliverables, hashes, disassembly and the detailed ARM review.
`feature-1088/` holds the original integration builds. The initial 1536-byte
builds are in `feature-pre-stop-fix/`, and the stop-fix builds preceding the live
UI fix are in `feature-pre-live-ui-fix/`. The c186 builds before stable authority
interpolation are in `feature-pre-authority-fix/`. The runtime-tested 68632
artifacts are in `feature-pre-command-fix/`; `feature/` holds final 7170 builds.
These local artifacts are evidence, not repository dependencies.

The baseline for independent-map growth is committed stage 1 `642704d219`.
Stage 2 comparisons use its archived feature builds. The memory/stack figures
below describe final 7170. The 782-test host suite passed after the authority
fix at 68632; 7170 changes no C++ code. Its 28-board generation/layout audit
and 84-conversion matrix passed. Comparing both final ELFs with 68632 found
all selected frames and call edges unchanged. Core8 section sizes are identical;
F7 adds 76 bytes of embedded INI data in `.rodata`.

## Software checks

| Check | Result | Evidence in stage 3 archive |
| --- | --- | --- |
| Host C++ suite | 782 tests from 141 suites passed | `authority-stability-host-full.log` |
| Converter unit tests | 12 passed | `conversion-tests-final.log` |
| Conversion matrix | 84 passed: 3 strategies × 28 board INIs | `command-conversion-matrix.log`, `conversion-matrix.json` |
| Generated definitions and expressions | 28 board INIs passed | `layout-report.md`, `command-layout-audit.log`, `command-all-board-generation.log` |
| Core8 final board build (7170) | Passed, default board flags | `core8-command-build.log`, `feature/core8/` |
| Proteus F7 final board build (7170) | Passed, default board flags | `f7-command-build.log`, `feature/proteus-f7/` |

Runtime regressions cover standalone compatibility; fractional TPS lookup;
captured-input validity and MAP-estimate provenance; endpoint and interior
authority; exact endpoint plateaus across fractional RPM/TPS and unsnapped
interior weights near both endpoints; corrections applied once; independent lambda/ignition loads; full
precision staging above the old packed-axis limit; strict fuel conversion;
and pure queries that do not publish state or change injection admission.
Gate tests cover startup readiness, faults despite additive fuel requests,
strategy changes, stale calculations, accepted-pulse drain, overlap/staging,
split continuations, prime suppression, stopped rearm and atomic batch failure.
The stop-timeout regression verifies that a recent tooth with zero-RPM
`SPINNING_UP` blocks rearm, then the slow callback transitions to actual
`STOPPED` after the trigger timeout without clearing the fault latch.

Conversion checks preserve the source map and axes without transposition,
retain the selected standalone strategy, and initialize every new field.
All readiness declarations remain false and authority remains zero. Invalid
axes, incompatible overrides, wrong boards, existing dedicated calibration
and unsafe overwrite attempts are rejected. These checks establish file and
numerical conversion behavior; they do not calibrate the new model maps.

## Persistent and live-data layouts

| Payload | Stage 1 → independent maps | Independent maps → composite | Total growth |
| --- | ---: | ---: | ---: |
| Persistent calibration | +1152 B | +100 B | +1252 B |
| Complete live-data block | 0 B | +32 B | +32 B |

The 1152 bytes are two 16 × 16 U16 maps and four 16-entry U16 axes. The
composite adds 96 bytes for its 8 × 8 authority table and two axes, plus a
4-byte readiness word. Pre-existing configuration offsets, shapes, types and
scales remain unchanged. The dedicated-table switch reuses an unused bit.

Core8 page sizes are 23736 → 24888 → **24988 bytes**. Proteus F7 sizes are
27736 → 28888 → **28988 bytes**; its 12-byte persistent wrapper gives 29000
bytes, leaving 3768 below the 32 KiB flash-storage boundary. The flash format
changes; this does not provide an in-place migration of an old raw tune.

Live data grows from 1356 to **1388 bytes**. New channels occupy offsets
**532–563**; old offsets below 532 remain unchanged, and later offsets move
by 32 bytes. The 1400-byte blocking factor leaves 12 bytes. Consumers require
the matching generated definition. Persistent offset compatibility must not
be mistaken for unchanged live-data offsets.

The 28-board audit checks table bindings, model-specific cursors, panel
enable/visibility expressions, stopped-engine controls and VE Analyze
conditions. Composite VE Analyze is disabled. `fome_48way.ini` is outside the
generator's 28-board list and is excluded from this conclusion. A structural
audit does not establish GUI behavior or live cursor correctness on hardware.

## Compiled memory

Values below are bytes from the archived default-flags ELFs. The final column
includes the **448-byte MainLoop reserve increase**. Calibration size alone
does not describe total RAM cost.

| Board / metric | Stage 1 | Independent maps | Final composite |
| --- | ---: | ---: | ---: |
| Core8 GNU text | 651902 | 653246 | 665518 |
| Core8 GNU data | 2504 | 2504 | 2504 |
| Core8 GNU BSS | 186880 | 188032 | 188640 |
| Core8 `.ram4` | 58320 | 59472 | 60080 |
| Core8 heap | 8680 | 8680 | 8636 |
| F7 GNU text | 691622 | 694818 | 708942 |
| F7 GNU data | 1332 | 1332 | 1332 |
| F7 GNU BSS | 458220 | 458220 | 458264 |
| F7 `.ram3` | 64880 | 66032 | 66640 |
| F7 heap | 66192 | 65040 | 64432 |

Independent maps add 1152 bytes to each calibration RAM region. Composite
integration adds another 160 bytes before the stack change; the final region
increase over stage 2 is **608 bytes = 160 + 448**. Core8 also loses 44 heap
bytes. F7's aggregate BSS includes its heap, hiding transfers from heap to
static storage. Final Core8 `.ram4` has 5456 bytes free; final F7 heap is 64432
bytes. GNU text includes read-only data, and F7 embeds the generated INI.
Both final bootloader S-records match their archived predecessors exactly.

## Stack review

The default LTO disassembly, including saved registers and actual call edges,
gives these selected normal paths:

| Path | Core8 | Proteus F7 |
| --- | ---: | ---: |
| Stage 2 SD with a redundant correction source | 700 B | 788 B |
| Final composite with a stored correction source | 916 B | 1028 B |
| Final MainLoop application-stack request | 1536 B | 1536 B |
| Arithmetic gap for the selected composite path | 620 B | 508 B |

At the original 1088-byte request those gaps were only 164/52 bytes. Rebuilt
ELFs confirmed unchanged selected frames after increasing the reserve. The
subsequent stop fix reduces both MainLoop frames from 112 to 104 bytes.
Core8 releases that frame before tail-calling the fast callback, so its path
total stays unchanged. F7 retains the frame for profiling and its path falls
by eight bytes. The subsequent stable-authority change reduces Core8's
composite evaluator frame by another eight bytes, from 216 to 208; its
selected deeper call path is unchanged. F7 retains its 216-byte composite
evaluator frame and selected path after the authority fix.
The earlier stage 2 report overcounted Core8's MainLoop frame and an inlined
sensor dispatcher; the table above corrects those sums.

The inspected EXTI → shaft handler → composite software paths total
1260/1260 bytes, excluding hardware exception entry and nested interrupts.
Both boards retain a separate 4096-byte exception stack. These selected sums
are not whole-program worst-case proofs or measured stack watermarks.
`stage3-arm-review.md` and `stage3-stack-evidence.txt` in the local archive
record the individual frames, addresses and final section checks.

## Simulator and hardware qualification

### Final simulator

The final simulator reports source `68632b305be899fd18285c6d7e582224ddb9336b`,
matching the runtime-tested ARM source. Final 7170 changes only the INI.
CRC-framed TunerStudio traffic had zero
protocol errors; configuration and output lengths were 24988 and 1388 bytes.
The RAM-only synthetic calibration passed 0/50/100% authority with flags
37/47/42 respectively, Ready status and no fault. Flat 100% remained exact
across commanded RPM 700–6999, including 900–903. The CLI accepts integer RPM;
exact fractional failure inputs are covered by the host regressions.

Clearing readiness while running latched Configuration, cleared current fuel
results and stopped the injection counter after pending callbacks drained.
Restoring readiness preserved the latch. Stopped rearm and restart recovered
Ready and advancing counters. Zero-RPM `SPINNING_UP` correctly refused rearm;
the simulator CLI could not isolate a single-tooth timeout, so the exact
watchdog transition remains covered by the host regression. The original
simulator configuration was restored byte-for-byte (CRC32 `15e41fa6`), sensor
mocks were reset, and the process stopped cleanly. Evidence:
`simulator-final-runtime-report.md` and its linked logs/results.

### Disconnected Core8 bench

The USB bench used synthetic calibration, sensor mocks and the internal
trigger emulator. Both final scripts identify `FOME core8 20260924@68632b305b`,
matching `feature-pre-command-fix/core8/metadata.json`. The final 7170
comparison above preserves its selected compiled paths.

- **Eight nominal checks passed:** readiness rejection; 0/50/100% authority;
  TPS fault; refusal to rearm while running; and recovery after stopping and
  explicit rearm. At 1500 RPM, 60 kPa MAP and 25% TPS, SD/Alpha-N masses were
  285.209/361.420 mg; the 50% result was 323.315 mg.
- **41 advanced checks passed:** common correction applied once; pedal-based
  correction validity and latched failure; calibrated MAP fallback versus
  missing readiness or an explicit measured-MAP requirement; and a strategy
  switch that stayed latched until stopped rearm. Four active corrections
  exercised eight auxiliary channels at 1500, 3000 and 6000 RPM.
- Final `threadsinfo` fill watermarks reported **808 bytes free for MainLoop**
  and **2724 bytes free for the ISR stack** after the advanced run. The nominal
  run reported 856/2816. Advanced CPU samples ranged **18–42%**. These are
  observed workload results, not worst-case timing or stack guarantees.
  The 6000 RPM samples include the existing injector duty-cycle cut; they
  establish continued composite calculation under that load, not uninterrupted
  injection at 6000 RPM.

Evidence: `hardware/nominal-bench.json`, `hardware/nominal-bench.log`,
`hardware/advanced-bench.json`, and `hardware/advanced-bench.log`. These final
results supersede the earlier c186 advanced watermarks of 800/2764 bytes.

### TunerStudio and defects found during qualification

TunerStudio MS Lite 3.3.01 connected to Core8 bench firmware 68632b305b showed
**Ready / None**. The SD editor cursor used **60 kPa**, and the Alpha-N editor
cursor used **25% TPS**, both at **1500 RPM**. Screenshots are
`ts-ui/online-ready.png`, `ts-ui/online-sd-cursor.png`, and
`ts-ui/online-alpha-n-cursor.png`.

Bench work exposed four defects. The first three fixes are included in 68632;
the fourth changes only the TunerStudio definition in 7170:

- Zero-RPM `SPINNING_UP` satisfied the old logical stopped test and bypassed
  the trigger-timeout reset. The watchdog now checks actual RPM state under
  the critical section and reaches actual `STOPPED` after timeout.
- The TunerStudio state/fault labels needed live-output subscriptions to
  refresh correctly. They now follow the subscribed output channels.
- Weighted-sum interpolation could produce 100.0000076 or 99.9999924 from a
  flat 100% authority table, causing a false Configuration latch or unwanted
  SD evaluation. Authority-only difference-form interpolation preserves the
  exact plateaus; fractional interior weights remain intact.
- The rearm button's raw `Erearm_airmass` user command transmitted only the
  `E` command byte. The 7170 template encodes the ASCII payload with explicit
  byte escapes. This preserves the firmware command and sends its full name.

With that patched INI and 68632 firmware, the real GUI showed **Fault latched /
Sensor**, then the stopped-engine rearm button changed it to **Waiting for
valid fuel / None**. Screenshots: `ts-ui/online-sensor-fault.png` and
`ts-ui/online-rearmed.png`. TunerStudio's saved dialog width clips the end of
the waiting label. The subsequent numeric readback confirmed **status 1
(NotReady), fault 0 (None), RPM 0**, and the ECU logged confirmation of
`rearm_airmass` after the actual button press. Evidence:
`hardware/gui-rearm-final.json` and `hardware/gui-rearm-final.log`.
A reopen failure was isolated
to TunerStudio's internal power-cycle listener; a fresh process loaded the same
INI and completed this check.

### Restoration

TunerStudio was closed, the emulator stopped and sensor mocks reset before
restoring the original ECU application. Full-flash readback was **1048576 bytes,
identical to the original**, SHA-256
`05241c2684ee31a9093cdc445e5b88a7106df7495faf080c5d9f12d1e810a3ed`.
The restored ECU reported original source **4c568dda55**, signature suffix
**601576114**, and **RPM 0**. Its 23736-byte calibration readback exactly matched
the saved original, SHA-256
`050f867e9d4c8d56a2e34bb1ddedc4226c165e5be7cc9a5ee87ab8f8c47babd9`.
Evidence: `hardware/restored-flash.json`, `hardware/restored-identity.json`,
and the corresponding full-flash/configuration readbacks. The bench ECU is
back on its original firmware and calibration.

### Limits

No injectors, ignition loads, physical crank/cam sensors or running engine
were connected. Counters and internal wave records establish software behavior;
this work does not qualify physical pulse timing, driver output, combustion,
calibration, electromagnetic interference or worst-case interrupt/sensor load.
Proteus F7 qualification is build/static review only. The observed Core8 stack
watermarks complement the selected disassembly paths; neither is a whole-program
worst-case proof.

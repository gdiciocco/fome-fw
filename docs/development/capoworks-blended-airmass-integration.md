# Capoworks blended airmass integration

## PR integration (2026-10-03)

Capoworks includes `feature/blended-airmass-pr` through `6d51a9003e` with its
calibration cache, prepared load cursors and per-cylinder publication, and reduced
synchronous trigger preparation. The complete PR also brings healthy-model
fallback and automatic recovery after a fresh valid fuel publication. The old
composite rearm command and button are removed; priming uses normal startup rules.
See the [current operator guide](../user/blended-airmass.md).

Capoworks CAN controllers, Hella sensor, verified flash writes, ADC lifecycle,
trigger lifecycle, VVT cranking threshold and HPFP pending-close guards remain.
Blocking factor stays at 1600 for the combined output-channel layout, and reserved
configuration policy bits retain their storage. The flash-write regression now
checks that neither a burn nor a retry admits injection before fresh fuel is
published, and that recovery then occurs automatically.

The combined engine suite passes **1041/1041 tests in 166 suites**; ADC lifecycle
suites pass **36/36 and 21/21** using `make -j12 test`. Configuration remains
25148 bytes, telemetry 1564 bytes and flash format 260930. Build and deployment
evidence is stored under
`/home/deffie/fome-artifacts/blended-airmass/capoworks-pr-integration-2026-10-03/`.

The records below describe earlier integrations and their validation results.

## Revision integration (2026-09-30)

Capoworks now includes `feature/blended-airmass` through `35bb993512`.
Dedicated SD/Alpha-N/MAF maps, independent consumer loads, Idle VE ownership,
shared temperature choice, standalone MAP multiplication and pure Alpha-N BARO
compensation follow the [accepted revision](blended-airmass-revision.md) and
[operator guide](../user/blended-airmass-it.md). Automatic conversion tools are
removed. The previous integration record below is historical.

The merge retains Capoworks shock/EMP enable bits and the retired dedicated-map
bit at its original bit 26. Persistent configuration grows by 60 bytes, from
25088 to 25148 on Core8. Flash format is 260930: old binary tunes are rejected;
restore calibration manually with the matching INI. Output channels grow from
1444 to 1564 bytes, with blocking factor 1600 to include protocol overhead.

Both branches contain the same full-width MLG fix. Trigger lifecycle, burn
verification, ADC lifecycle, DC output validity and HPFP pending-close guards
are preserved. Airmass dependency checks now honor both VVT minimum RPM and
the Capoworks cranking threshold; the added boundary regression ensures an
inactive VVT source cannot block fuel. Both branches’ VVT/HPFP tests are retained.

The combined host suite passes **987/987 tests in 164 suites**, and the two
ADC lifecycle suites pass **36/36 and 21/21**. The five Capoworks HPFP lifecycle
fixtures now explicitly provide measured MAP for their independent target source;
their stop/restart assertions remain in place.

Current integration build/test/deployment evidence is archived under
`/home/deffie/fome-artifacts/blended-airmass/capoworks-integration-2026-09-30/`.
This directory is separate from the standalone branch’s qualification evidence.

## Previous integration (2026-09-29)

Integration source: `7a0bf148df`, recorded on 2026-09-29; the subsequent
MLG logger fix is `8ffd996cc6`. The merge brings
`feature/blended-airmass` at `9fa5c6c8d0`, including upstream master
`b5142419ec`, into Capoworks `13ad13d5d1`.

## Layout choices

The existing Capoworks enable bits remain at offset 580: shock preload bit 5,
EMP pump bit 6 and post-cranking mode bit 25. Dedicated airmass tables use
previously unused bit 26. The standalone feature branch uses a different bit;
use the INI generated for the selected branch.

All 1545 existing persistent declarations retain their offsets, types, scales
and bit indexes. Upstream fan speed thresholds occupy padding bytes 327 and
330. Independent maps and blend settings add 1252 bytes at the end of the
calibration. The Core8 page is 25088 bytes. Flash format version is **260929**;
existing raw flash tunes are not migrated in place. Back up and restore the
tune using the matching definition, then prepare the new maps before enabling
blended operation.

Existing Capoworks telemetry occupies bytes 532–587. Blended telemetry follows
at 588–619, and subsequent channels move by 32 bytes relative to the previously
generated Capoworks layout. Complete live data is 1444 bytes; the blocking
factor is 1460, satisfying the protocol's additional 10-byte requirement.
Some checked-in generated INIs predate Capoworks telemetry and therefore show
a larger difference. Builds regenerate the definitions from source.

## Runtime integration

- MainLoop retains the feature's 1536-byte reserve. The Capoworks flash writer
  retains its doubled utility-thread stack.
- Composite callback admission remains atomic; Capoworks trigger cancellation
  and ignition discharge protections remain in place.
- Delayed priming checks driver throttle intent and drains callback accounting.
  Composite mode continues to suppress priming.
- Failed calibration verification and a successful retry preserve the composite
  fault latch. Recovery still requires explicit rearm after stopping.
- Upstream's configured DFCO timing ramp coexists with Capoworks startup
  inhibition. The regression now enters actual RUNNING state and waits for the
  configured startup delay before expecting fuel cut.

## Combined SD log header

The combined simulator logs 768 fields, producing a 68376-byte MLG header.
The old writer serialized only the lower 16 bits of the data offset and rejected
a header above 65535 bytes. The MLG v2 format defines this offset as 32 bits;
the writer now emits all four bytes. All telemetry fields remain present.
Compile-time checks retain the separate 16-bit limits for record length and
field count. Regression cases cross the 64 KiB boundary and exercise all four
offset bytes.

## Validation

After the MLG correction, the merged engine suite passed **943 tests in 159
suites**. The ADC lifecycle suites passed **36 and 21 tests**. New integration regressions cover delayed
prime/flood clear and failed-burn/retry latch preservation; the upstream DFCO
ramp test also checks the startup holdoff. The trigger scheduler interleaving
test double explicitly rejects unexpected batch scheduling.

The simulator clean build and 10-second smoke run passed. Its actual MLG file
contains all **768 fields**, a **68376-byte** data offset and **1413-byte**
records. All **497 complete records** passed checksum and rolling-counter
checks. The timed exit left a 1022-byte partial next record in the file; that
trailing fragment was excluded from complete-record validation.

The synchronized standalone branch separately passed **786 tests in 141
suites** and its Core8 build. Its configuration/output sizes remain 24988/1388
bytes and RAM allocation is unchanged from the prior standalone qualification.

The final Core8 build and illegal-conversion check passed at `8ffd996cc6`.
The converter additionally passed **14 tests** and all three SD/Alpha-N/MAF
fixtures against the generated Capoworks Core8 INI. It accepts the reviewed
bit-5 and bit-26 layouts and preserves named shock/EMP settings. Subsequent
converter and documentation changes do not change the firmware.

The reviewed Core8 paths use 932 bytes for composite calculation and up to
444 bytes for the inspected blocking-flash paths. These execute sequentially
and fit the 1536-byte MainLoop request. The selected EXTI path uses 1276 bytes
on the separate 4096-byte stack, excluding hardware entry and nested interrupts.
These are selected paths, not worst-case bounds or measured watermarks.

The Core8 image has 4408 bytes of free CCM and an 18728-byte allocator heap.
Its existing Capoworks profile disables SD file logging while retaining USB
MSD; its firmware size is therefore not directly comparable to the standalone
feature profile. The simulator build exercises the MLG writer.

Build and generated-layout evidence is archived under
`/home/deffie/fome-artifacts/blended-airmass/sync-2026-09-29/`.
The previous [bench validation](blended-airmass-validation.md) identifies its
own earlier firmware revisions. No USB flashing or hardware qualification was
performed for this synchronization; the ECU retains its restored original
firmware and calibration.

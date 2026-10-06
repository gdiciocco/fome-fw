# Experimental ignition timing and dwell planning

TunerStudio exposes two independent settings: **Ignition scheduling: Legacy / Cycle profile (experimental)** and **Dwell planning: Legacy / Time budget (experimental)**. Both default to Legacy. RPM, idle feedback, and injection retain their existing timing policies.

The settings occupy a new word at the end of `engine_configuration_s`, not previously unused bits. On the current upstream layout, engine configuration grows from 3800 to 3804 bytes and the tune page from 23832 to 23836 bytes. The existing flash loader rejects a stored-size mismatch and restores defaults. Export and migrate a tune with matching firmware/INI definitions. A binary tune from a custom branch requires its own field mapping; a raw copy or assumption that old padding is zero is unsafe.

## Ignition safety, including Legacy mode

Each main ignition occurrence owns a generation, output-mask/cylinder/dwell snapshot, and Pending/Charging/Closed state. Timer actions carry the slot and generation by value in one machine word. Accepted LOW closes the occurrence and cancels pending HIGH, angle membership, and normal firing timer before record reuse. A firing callback that overtakes its pending charge closes an uncharged occurrence rather than allowing a later orphan HIGH. An old occurrence cannot discharge a newer owner's coil.

Pending charge estimates use a separate generation. Revising a pending timer invalidates its extracted old action without changing the firing occurrence or physical owner. Multispark pulses open distinct main occurrences. All transition/cancellation/registration operations use the existing scheduler critical-section discipline, including its inline execution behavior.

The 27-bit generation counters skip zero. Wrap safety relies on the executor's bounded callback lifetime: a copied callback runs synchronously under scheduler serialization and cannot remain outstanding across 134,217,727 reuse operations. Waiting and due angle entries remain reachable for cancellation. This is not an API for retaining callback tokens indefinitely outside the executor. Boundary tests exercise both counters around wrap.

Every physical ignition output, including trailing coils, owns an independent timer. At a new LOW-to-HIGH transition it records its first HIGH and reserves a deadline at 1.5 times the captured nominal dwell. The deadline is reserved before HIGH; the HIGH timestamp is taken at the output setter, so setup time conservatively shortens the programmed interval. Timer service/interrupt latency can delay the actual LOW: this is a software timer safeguard, not an independently clocked hardware current limiter.

Repeated HIGH or acquisition by the same owner does not renew age/deadline. A different owner loses contention; a paired mask is checked before any member is energized. A physical LOW, including one through `OutputPin*`, ends ownership and cancels the guard. The guard validates its own physical epoch, attempts the occurrence's normal cleanup, and forces LOW even if that occurrence has already closed. An obsolete physical timer cannot turn off a newer pulse.

Minimum dwell remains 0.8 times the occurrence's snapshot. Only outputs actually charged by that owner participate. With unequal output ages, the latest minimum-dwell time is limited by the earliest physical hard deadline; the cap wins. A dwell/voltage-table update does not rewrite an active pulse's snapshot. New main/trailing HIGH and additional multispark pulses recheck ignition enable and current cut state; existing charged outputs retain their LOW and guard.

Trailing charge has independent pending identity, cylinder and dwell snapshots. It cannot inherit a subsequent multispark pulse's shorter dwell. Its existing angular offset conversion is retained, but its physical cap can terminate it before the trailing target, including during a long multispark sequence. Raw ignition GPIO/bench HIGH also gets the current nominal dwell cap, and invalid or sub-10-microsecond nominal dwell cannot energize an output. Valid nominal requests are bounded to 0.01–1000 ms for finite timer conversion; this is not a coil-specific acceptable dwell range. Existing multispark OFF delay is preserved. No universal coil recovery/OFF-time calibration is introduced.

## Supported prediction geometry and history

Initial support covers four-stroke, single-primary, rising-only missing-tooth crank wheels with full 720-degree cam phase synchronization, including 36−2 and 60−2. Geometry validation checks ascending distinct references, paired duplicate waveform slots, identical geometry in both revolutions, and two equal gaps. Other shapes, unknown phase, two-stroke operation and direct self stimulation use Legacy conversion. Time-budget planning additionally requires Individual Coils; shared-coil and wasted modes retain Legacy planning with the safety rules above.

For accepted reference `j`, with `N` references in 720 degrees:

```
nextDuration = (t[j-N+1] - t[j-N]) * (t[j] - t[j-3]) / (t[j-N] - t[j-N-3])
```

A three-entry FIFO captures overwritten history before the existing Instant RPM timestamp write. No second timestamp ring advances independently. Prediction first becomes available at `j=N+3` (72 trusted timestamps for 36−2). The cache matches the exact full timestamp, trigger phases and engine phases. Current-tooth ignition conversion uses the cache; distant sparks remain angle queued until their final interval. Nonignition callers retain the default conversion.

Stop, timeout, synchronization/phase changes, relocated pre-sync history and waveform changes invalidate readiness. Discontinuous/repeated indices, nonmonotonic timestamps, pauses above 100 ms, malformed geometry, zero/stale spans and nonfinite results reject prediction. Individual intervals are limited to 100 ms and historical cycle spans to 4.8 seconds. Differences use wrap-safe uint32 arithmetic, with full-width timestamp checks for long pauses; timestamp zero is valid.

Historical adaptation is accepted only in `[0.5, 2]`, without clamping. When rejected, a local last-interval conversion is permitted only if all three individual matched recent ratios agree within 10% (`min >= 0.9 * max`), after structural validity checks. This local fallback never enables historical long-horizon ETA. Ordinary valid updates currently share two float32 divisions between local conversion and the planner; coherent-rejection handling adds divisions only on that path. ARM measurements must include these actual costs.

## Time-budget planning

Before arming, each tooth uses the live advance/knock target and current nominal dwell. The target is anchored to its cylinder's TDC cycle, including advance/retard across the 0-degree boundary. It is not identified by a heuristic angular latch. A target already passed within the bounded horizon is counted as expired and skipped for that TDC occurrence; moving it later cannot revive the same occurrence.

ETA sums matching historical intervals, scaled by recent-three adaptation, with a maximum horizon of 180 degrees and 32 references. Only the final partial interval needs interpolation/division. History comes from the existing timestamp ring plus the saved old-current timestamp. Unavailable/rejected history uses legacy RPM conversion within the same horizon; unsupported geometry uses the original planner.

Charge is armed when `ETA - dwell` enters the predicted current interval. An exhausted positive target budget admits immediate charge, with unchanged minimum dwell and hard cap. The firing target is then latched. A new tooth may cancel/revise only Pending charge, before angle promotion; Charging pulses and their deadlines never move. The planner is bounded but can be expensive when many targets share a tooth. It remains experimental even if an aggregate accuracy percentile improves.

## Evidence and limits

Native regression tests cover occurrence/charge/physical-epoch lifetime, same-tick orders, stale callbacks, wrap boundaries, stop/cuts/configuration flush, paired age/ownership, external LOW, multispark/trailing, changing dwell, queued promotion and electrical 36−2/60−2 crank/cam streams with a missing or extra pulse. Optional decoded-phase replay uses the corrected expected event count, actual Instant RPM selection, explicit initial LOW and end-censored HIGH reporting.

Accuracy comparisons must include event population, matched occurrences, missing/extra discharges, uncharged callbacks, continuous HIGH duration, end censoring, guard counts and changed-target cohorts. Completed-spark percentiles alone can conceal a stuck coil. The recorded tooth log does not establish physical TDC or speed within an interval; rescaling its low-speed ripple to high RPM is synthetic stress evidence. Host execution times are not STM32 timings. USB/DWT/logical-output tests do not measure coil current, electrical propagation, true crank angle or worst-case interrupt latency, and do not establish automotive qualification.

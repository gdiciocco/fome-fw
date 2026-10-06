# Experimental ignition cycle profile scheduling

In TunerStudio, **Ignition scheduling** selects **Legacy** (default) or **Cycle profile (experimental)**. The setting occupies reserved bit 5 at configuration byte 580; existing field offsets and the persistent configuration size are unchanged. It is independent of Instant RPM selection and idle feedback settings.

The prototype changes the final angle-to-time conversion for main coil charge and firing, including immediate firing and promotion from the angle queue. Distant events remain in that queue until their final tooth interval. Injection, RPM sensors, `oneDegreeUs`, global phase interpolation, trailing spark offsets, and dwell start angle planning retain their existing behavior. Minimum dwell, over dwell protection, limiter, knock and multispark retain their existing action lifecycle. In particular, predicting the next tooth accurately does not prevent minimum dwell from postponing a spark.

Initial support covers four-stroke, single-primary, rising-only missing-tooth crank wheels with full 720-degree cam phase synchronization, including 36−2 and 60−2. Configuration validates the decoded geometry: ascending distinct rising references, paired duplicate waveform slots, identical geometry in each revolution, and two equal gaps. Logical tooth count and neighboring references derive from this geometry; no fixed 68-tooth history is allocated. Mixed primary/secondary shapes, multiple edge modes, cam-mounted wheels, two-stroke operation self stimulation, and unknown cycle phase fall back to Legacy.

For the current logical tooth `j`, with `N` references per 720-degree cycle, prediction is

```
nextDuration = (t[j-N+1] - t[j-N]) * (t[j] - t[j-3]) / (t[j-N] - t[j-N-3])
```

All timestamps are historical or current. The three-interval window can cross a gap: on 36−2 it then spans 50 degrees instead of 30. History is captured before Instant RPM overwrites the same-phase timestamp. A three-entry FIFO saves those overwritten timestamps; the existing timestamp write and all its consumers are preserved. Prediction begins at `j=N+3`: 71 completed intervals / 72 trusted timestamps for 36−2. Startup in the middle of a cycle follows the same rule; pre-sync timestamps do not count toward readiness.

One float32 division computes ticks per degree once per accepted tooth. Each local ignition event uses one multiply. Cache lookup requires the same full timestamp, trigger phases and engine phases. Current-phase targets receive zero delay; a target at the next boundary belongs to the next tooth. Requests outside the current interval, with a stale cache or without phase context use the original RPM conversion.

Stop, timeout, loss of synchronization, relocation of pre-sync timestamps, changed cam phase adjustment and waveform/configuration changes invalidate readiness. Repeated confirmation of the same cam adjustment preserves it. A discontinuous/repeated index, nonmonotonic timestamp, tooth pause over 100ms, malformed phase geometry, zero/stale history or nonfinite result rejects prediction. History spans are bounded by the 4.8-second cycle duration at the existing 25 RPM stop threshold; each old/predicted interval is bounded by 100ms. Differences use wrap-safe uint32 arithmetic, while a full-width timestamp checks pauses across multiple wraps. Zero timestamps are valid.

The adaptation ratio must be in `[0.5, 2]`. This is an experimental rejection policy, not a validated engine limit; a legitimate sudden speed step can invoke fallback. It is not clamped. Tests include the inclusive endpoints and rejected larger steps; diagnostic replay reports raw mathematical prediction separately from this guarded implementation.

Interpolation assumes constant speed within a tooth interval. Tooth logs do not independently establish speed between edges or absolute physical TDC. Recorded low-speed ripple rescaled to high RPM is a stress replay, not measured high-speed engine behavior. A changed combustion profile, imbalance or misfire can defeat historical prediction. Keep this feature experimental pending hardware validation.

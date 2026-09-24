# Slow ADC gap capture (Core8)

The Core8 main loop normally updates the slow ADC every two 1 ms iterations. To
investigate a gap longer than 10 ms:

1. From this branch, run `bash .vscode/scripts/core8_dfu.sh rebuild` at the
   repository root. Flash the resulting Core8 image and use its matching
   `firmware/tunerstudio/generated/fome_core8.ini` in TunerStudio. Copy the INI
   before switching branches: generated build files are not stored in Git.
2. Record an MLG datalog. The `ADC gap:` channels capture the last gap and all
   main-loop iterations since the previous ADC update whenever the gap exceeds
   10 ms.
3. In the FOME console, open Bench Test and click **Grab ADC Gap Trace**. The
   trace records continuously in a ring buffer, stops at the first gap over
   10 ms, and saves `*_adc_gap_trace.json` in the console's working directory.
   Open the file in a trace viewer compatible with Chrome trace JSON. The
   `SlowAdcGap` marker is the trigger. Run `threadsinfo` in the console to map
   numeric thread IDs in the trace to thread names.

`Loop work total` sums elapsed time inside the main loop between the preceding
ADC start and the delayed ADC start. `Outside loop total` sums time between
main-loop iterations in the same interval. The ADC, ETB, slow callback, and
fast callback values are the longest individual calls in that interval. A
slow callback also records the combined module time and the indices and times
of its three slowest modules. Module indices are zero-based positions in
`Engine::engineModules` in `firmware/controllers/algo/engine.h` (including
`modules_list_generated.h`). A large `Outside loop total` means the loop was
sleeping or unable to run; inspect the trace just before `SlowAdcGap` for long
interrupts and context switches. A large `Loop work total` points to work or
preemption within the loop. These are wall-clock durations, so a section may
include time spent in an interrupt.

The trace uses the firmware's shared 8 KiB big buffer. If the buffer is being
used by another logger, the console reports that it cannot arm the capture.
The capture waits up to 30 seconds for a gap, and leaves the ordinary
**Grab PTrace** behavior unchanged.

## Core8 capture on 2026-09-24

A captured slow ADC interval lasted 10,996 us. The preceding slow callback took
9,484 us, including 9,372 us in engine modules. Module index 31 (`ShockPreload`)
alone took 9,291 us; the next two modules took 28 and 21 us. ADC conversion
never exceeded 602 us in that session.

Before the fix, `ShockPreload::onSlowCallback()` polled over CAN every 100 ms. Its
`CanTxMessage` destructor calls `canTransmit()` with a 10 ms timeout from the
main-loop thread. During a 10 second live check, `canWriteNotOk` rose from 572
to 637 and slow ADC gaps rose from 572 to 637. The failed CAN transmission is
therefore the cause of these main-loop stalls and thermistor timeouts.
ChibiOS waits for a free TX mailbox (or for CAN to leave sleep mode); this
capture does not establish why transmission is unable to proceed.

The fix moves that poll to the CAN TX thread. In a 30 second Core8 check after
the fix, 912 CLT/IAT samples had no reported sensor errors while
`canWriteNotOk` increased from 130 to 397. The CAN transmission failure itself
remains to be investigated separately.

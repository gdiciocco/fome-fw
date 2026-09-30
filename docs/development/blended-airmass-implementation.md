# Airmass revision implementation record

Worktree: `/home/deffie/worktrees/fome-blended-airmass`.
Branch: `feature/blended-airmass`. Starting commit: `f75e554863`.
Authoritative requirements: [revision](blended-airmass-revision.md).
Artifacts: `/home/deffie/fome-artifacts/blended-airmass/revision-2026-09-30`.

## Scope and sequence

1. Implement dedicated SD/Alpha-N/MAF maps, captured shared temperature,
   standalone optional MAP multiplier, pure Alpha-N optional BARO correction,
   general MAP-estimate permission and model-owned Idle VE.
2. Give each independently calibrated downstream table/control an independent
   load source, including per-cylinder trim/knock tables and indirect GPPWM uses.
   Keep control values at full precision and expose matching display cursors.
3. Update TunerStudio controls, labels and map availability. Qualify composite
   VE Analyze only for a uniform 0/100% authority map for a complete running
   session, with Idle VE disabled. A configuration write invalidates qualification
   until the next stopped/running session; observe a ten-second startup exclusion.
4. Complete regression tests, host tests and serialized Core8/Proteus F7 builds.
   Check persistent layout, flash, RAM, output-packet and stack headroom.
5. Back up the attached isolated Core8, test the built application and matching
   TunerStudio project, then restore the original firmware and calibration.
6. Finish operator documentation and accepted-requirement traceability. Remove
   automated tune-conversion deliverables; document manual backup/copy/review.
   Complete the MAP-estimate BARO normalization study without enabling an
   unqualified physical model. Engine calibration remains outside a bare ECU test.

## Working decisions

- Map dimensions remain unchanged. Added selector bytes and diagnostic channels
  are measured separately from table storage. The serial blocking factor must
  cover the generated output structure plus protocol overhead.
- The strategy enum remains canonical: SD, Alpha-N, SD + Alpha-N, MAF and Lua.
  There are no redundant model-enable flags.
- New selector defaults mean the native model load independently; manual tune
  restoration must select the actual old source for consumers which previously
  inherited the ignition or lambda selector.
- Existing tCharge estimator fallbacks remain: numeric validity of tCharge does
  not prove both IAT and CLT sensors are healthy. Selected measured IAT is strict.
- Startup BARO no longer substitutes 101.325 kPa for a missing MAP reading.
- No network publication is part of this implementation task.

## Team and ownership

- Astra high (`revision_core`): airmass models and model regression tests.
- Astra high (`consumer_inventory`): downstream consumer audit, implementation,
  and focused actuator/control regressions.
- 5.6 Sol high (`revision_ui`): TunerStudio controls/cursors and VE Analyze audit.
- Root: shared schema/defaults, captured load publication, live fuel admission,
  integration, serialized verification, bench and final documentation.

## Verification status

Implementation at `88f07ff8f0`, all 831 host tests, Core8/Proteus F7 builds and
the complete 55-sample Core8 bench sequence have passed. Interactive TunerStudio
editor/cursor checks passed. Original ECU flash and configuration were restored
byte-for-byte, with RPM zero. The [final validation record](blended-airmass-revision-validation.md)
defines the tested scope and remaining engine-qualification limits. Earlier stage-3
build/bench evidence belongs to the previous implementation; it is used only as
an explicitly named comparison baseline below.

Unrelated initial changes preserved: `.vscode/settings.json` and the
`java_console/luaformatter` submodule IDE metadata.

## Historical integration checkpoints

The following entries retain the evidence and outstanding work at each checkpoint;
the verification status above and the final validation record supersede them.

### Integration checkpoint (2026-09-30)

- Core, downstream consumers and TS/operator guide implemented; source frozen for
  ARM/bench qualification. No commit or network publication yet.
- First host suite: 791/802; second: **813/813 passed**. Remaining final MAF
  numeric-input test was added afterward; final suite runs separately.
- Generated host layout: config **25,048 bytes** (+60 versus prior feature),
  outputs **1,508 bytes** (+120), serial blocking factor **1,536 bytes**.
  Firmware assertion requires output size +10 <= blocking factor.
- Fixed stale masses/durations on invalidation. Standalone SD/Alpha-N/MAF now
  require valid current publication to admit injection, recovering automatically;
  composite faults retain their latch/rearm behavior. Accepted pulses still drain.
- Automated converter and synthetic conversion fixtures removed; manual
  restoration from TunerStudio backup is the supported workflow.
- Fresh Core8 identity is capoworks.2026.09.29.core8.3263625221,
  version e2390abf2c, **not** the older stage-3 original. Current config is 25,088
  bytes, SHA256 c90dc06ca762abdd3e9650356bc696ee60a84c09e2cda33e5811c2263e8b289f.
- Two complete 1MiB flash reads match SHA256
  e61982ff60a5475c7c233c0907fa6fc9f4cb3acff783792edda09c192920cc12.
  Saved current INI/config/flash under artifact `hardware/`; prepared
  `restore-original-application.srec` for 0x08008000..0x080FFFFF.
  Bootloader unchanged. Original application rebooted and signature verified.
- TunerStudio process 21884 is offline with the owner's project modified in RAM;
  preserve it. Test through a separate project/instance, do not save over it.
- Agents hit a usage limit, then resumed successfully after the user's continue;
  completed work was retained. No usage-reset tool was invoked.

### ARM integration findings

The first Core8 build caught the existing 16-bit MLG header limit after adding
diagnostic fields. Reused the already reviewed Capoworks fix from `8ffd996cc6`
(full 32-bit MLG data offset, width guards and independent boundary tests).
No logged fields were removed. A read-only integration review also found a
possible stale load snapshot publication after a concurrent stop/config write;
captured epoch/version checks and regression tests now reject that stale publication.
Final verification must include these fixes; the earlier 814-test pass predates
them.

### Final integration checks

- Host regression suite after publication, boost-input and MLG fixes: **821/821 passed**.
  A subsequent coordinated fix removes inactive ignition/lambda dependencies,
  while retaining lambda targets required by active STFT or lambda protection.
  The final suite must include that change.
- `bash gen_config.sh`: all **28 boards** generated successfully. Artifact
  `layout_audit.py` verifies unchanged main/idle/authority map dimensions,
  encodings and offsets, all constant bounds, dedicated Idle VE enum, and output
  packet bounds. Configuration increases by 60 bytes on every board.
- Proteus F7 generated configuration: **29,048 bytes**, persistent wrapper
  **29,060 bytes**, leaving **3,708 bytes** below 32 KiB. Outputs 1,508 / blocking factor 1,536.
- MLG prerequisite committed separately as `498e542e92`.

### Firmware qualification

- Final host suite: **827/827 tests passed** (`host-qualified-tests.xml`).
  The final regression fixes preserve prepared trailing timing across enable
  transitions, and validate inactive lambda/ignition dependencies against actual
  STFT/monitor use. Implementation commit: `d5459bbb58`.
- Official serialized `compile_core8.sh` and `compile_proteus_f7.sh`: **PASS**.
  Artifacts, INIs, linker maps, symbols and disassembly are archived in
  `feature/core8` and `feature/proteus-f7`.
- Core8 application: 681,120/753,664 bytes, leaving 72,544 bytes; RAM4
  60,416/65,536 bytes, leaving 5,120 bytes; linker heap 8,240 bytes.
- Proteus F7 application: 726,884/753,664 bytes, leaving 26,780 bytes; RAM3
  66,976/131,072 bytes; linker heap 64,096 bytes.
- Compared with the archived stage-3 feature, RAM4/RAM3 increase 336 bytes;
  Core8 heap decreases 396 bytes and F7 heap decreases 336 bytes.
  Runtime stack low-water marks still require the current bench run.
- Core8 candidate flashed as an application-only OpenBLT update; signature
  `rusEFI (FOME) feature/blended-airmass.2026.09.30.core8.1693328201`,
  version `FOME core8 20260929@d5459bbb58`. Confirmed minimal-pins engineType 99,
  stopped engine and matching INI before starting the RAM-only bench fixture.

### Bench findings and final follow-up

- The first candidate reproduced standalone pulse starvation after a restart at
  constant RPM despite valid calculated mass. `beginCalculation` cleared admission
  on every fast callback, permitting repeated alignment with injection teeth.
  Commit `21b6718e5e` retains the last completed same-mode/version publication
  during recalculation. Confirmed failures close admission immediately with a
  token guard. Added interleaved scheduling and immediate failure regressions.
- Post-fix host suite: **830/830 passed**. Both official ARM builds passed.
  Commit `2f2d0fed8f` marks unsupported load-source codes 6/7 as `INVALID`;
  actual TunerStudio dropdown inspection confirms those choices are hidden.
- The second bench run passed model arithmetic, thermal/BARO policies, independent
  cursors, MAP-estimate permissions, all three standalone fault/recovery cases,
  and composite drain/rearm. It then hit `PWM nesting issue` at the 25th simulator
  enable. The PWM stop early return increments its nesting counter without
  decrementing it. This is a simulator restart accounting bug, independent of
  low-RPM selection; a separate narrow fix and repeated-stop test are in progress.
- Evidence from both incomplete runs is retained as `hardware/pre-admission-fix-*`
  and `hardware/pre-pwm-stop-fix-*`; their firmware artifacts are retained in
  `feature-pre-admission-fix/` and `feature-pre-pwm-stop-fix/`.
- ECU remains on the candidate application, stopped with self-stimulation disabled.
  Original application/configuration restoration and complete final bench/GUI
  qualification are still required before completion.

## Final completion

- Source `88f07ff8f0`: **831/831 host tests**, both official ARM builds, all
  28 layouts and **55/55 isolated Core8 bench samples** pass. No bench skips,
  errors or cleanup errors. `88f07ff8f0` fixes PWM stop-counter accounting;
  `PWM.RepeatedStopRestartBalancesCallbackNesting` exercises 40 cycles.
- Final application flash free: Core8 72196 bytes, Proteus F7 26616 bytes.
  RAM4/RAM3 and heap figures are unchanged from the prior qualification entry.
  Observed MainLoop/ISR stack margins are 696/2620 bytes for this fixture.
- Separate TunerStudio 3.3.01 project verified dedicated editors, independent
  MAP/TPS live cursors and the SD analyzer binding. No Auto Tune or calibration
  write was performed. Analyzer tabs remain visible regardless of internal
  eligibility. Cosmetic enum-placeholder warnings are recorded in the GUI report.
- Original `e2390abf2c` firmware/configuration restored. Two complete flash reads
  match the original 1 MiB backup; two runtime configuration reads match all
  25088 saved bytes. Final RPM zero. Owner project and unsaved edits preserved.
- Operator guide includes manual source restoration, composite stop/drain before
  strategy changes, temperature/pressure choices, all new selectors and limits.
  Documentation relative links and `git diff --check` pass. Generated build files
  archived/restored; initial VS Code/submodule metadata changes remain untouched.
- Local commits separate the MLG prerequisite, main revision, bench-driven
  admission/PWM fixes, enum UI adjustment and final documentation. No push or
  PR publication. The draft PR text remains in the artifact directory.
- Table resizing remains excluded. MAP-estimate BARO normalization remains a
  completed study without runtime adoption. Engine/exhaust/thermal/altitude
  qualification requires measurements beyond this isolated ECU bench.

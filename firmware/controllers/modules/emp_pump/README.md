# Caponord EMP coolant pump

This module controls the Caponord electronic coolant pump over extended
29-bit CAN/J1939. It is a native FOME `EngineModule`/`CanListener`
implementation for the Core8 target.

The protocol, state machine, and control strategy were ported from
`gdiciocco/speeduino`, branch `caponord-stm32-optimized`, commit
`df1df13547cccc9b4875227c28450d13b3f72872` (`emp pump`). The original
author has explicitly authorized reuse. The FOME implementation is an
architecture-specific adaptation rather than a direct source copy.

## Hardware and CAN setup

Core8 provides the expected bus without low-level driver changes:

- FOME `Bus0` / MCU CAN1
- RX `PD0`, TX `PD1`
- 500 kbit/s
- extended 29-bit identifiers

The pump and ECU must share CAN ground and the bus must be terminated in the
normal way. The module does not change the configured CAN bitrate. FOME CAN
read and write must both remain enabled.

The shock-preload controller uses `Bus1` / CAN2 with standard identifiers
`0x720` through `0x722`, separate from EMP's default `Bus0` / CAN1.

## Protocol

With `nn` as the pump/controller address and `ss` as the ECU source address:

| Direction/message | Extended CAN identifier |
| --- | --- |
| ECU to pump command | `0x18EF0000 | (nn << 8) | ss` |
| Pump status 1 | `0x18FF0300 | nn` |
| Pump status 2 | `0x18FF2300 | nn` |
| Pump status 3 | `0x18FF2400 | nn` |
| External temperature | `0x18FF4300 | nn` |
| Address claim | `0x18EEFF00 | nn` |

The defaults are controller address `0x96` and ECU source address `0xA3`, so
the default command identifier is `0x18EF96A3`.

Commands are eight bytes long. Pump speed uses 0.5 RPM per bit in little
endian order:

```text
rawSpeed = requestedRpm * 2
```

An off command uses `0xFFFF` in the speed bytes. The first command byte is:

| Value | Meaning |
| --- | --- |
| `0xFC` | Pump off, do not change Power Hold |
| `0xF0` | Pump off, release Power Hold |
| `0xFD` | Forward operation, do not change Power Hold |
| `0xF1` | Forward operation, release Power Hold |
| `0xF5` | Forward operation, request Power Hold |

The regulator runs at 10 Hz. A changed command is queued on the next slow
callback and repeated every 500 ms as a heartbeat. The selected bus's CAN TX
thread transmits queued commands.

## Operating states

The module implements these states:

- `Disabled`: configuration is disabled or invalid.
- `Stopped`: enabled, with no active engine, after-run, or service request.
- `EngineActive`: the engine is running, or cranking when configured to run
  the pump during cranking.
- `AfterRun`: coolant remains above the after-run threshold after engine stop.
- `ServiceTest`: a bounded diagnostic request from TunerStudio.

Thermal diagnostics further classify operation as inactive, warm-up,
closed-loop, capacity-limited, overload, after-run, failsafe, or service.

## Control strategy

When CLT is valid, normal engine operation can use either:

- a six-point CLT-to-pump-RPM fallback curve; or
- closed-loop CLT control.

Closed-loop control combines:

- a four-point minimum pump-flow curve indexed by engine RPM;
- proportional and integral CLT correction with deadband, integral limiting,
  and conditional anti-windup;
- MAP-by-engine-RPM load feed-forward;
- filtered IAT compensation;
- vehicle-speed and radiator-fan airflow relief;
- positive coolant-temperature-slope feed-forward;
- output limiting and configurable RPM ramping.

Invalid CLT while the engine is active selects the configured failsafe RPM.
Invalid IAT disables only IAT compensation and sets its diagnostic fault.

The default calibration is conservative and EMP remains globally disabled
after a configuration reset. Important defaults include:

| Setting | Default |
| --- | ---: |
| Minimum running RPM | 1500 RPM |
| Maximum RPM | 6000 RPM |
| Failsafe RPM | 3000 RPM |
| After-run minimum | 1800 RPM |
| After-run start/stop | 95 / 85 C |
| After-run maximum | 180 s |
| Battery cutoff/resume | 11.5 / 12.0 V |
| CLT target | 90 C |
| Command ramp | 2000 RPM/s |
| Service speed/duration | 2000 RPM / 10 s |

Configuration validation rejects invalid CAN addresses, invalid or unordered
curve axes, inconsistent RPM limits, inverted temperature or battery
hysteresis, and gains outside the ranges exposed by TunerStudio. Invalid
configuration leaves the pump disabled and raises the configuration fault.

## After-run and Power Hold

After-run begins after engine stop when CLT is at or above the configured start
temperature. It ends when any of these conditions is met:

- CLT reaches the stop temperature;
- the configured maximum duration expires;
- CLT becomes invalid.

Battery cutoff has hysteresis: the pump command is temporarily inhibited below
the cutoff and may resume only at or above the resume voltage while the
after-run window is still valid.

Power Hold is a pump-controller function. It does **not** keep the ECU powered.
Core8 currently has `EFI_MAIN_RELAY_CONTROL` disabled and no default
`mainRelayPin`, so post-key-off after-run is best-effort and lasts only while
the ECU remains powered. Guaranteed post-key-off cooling requires appropriate
vehicle wiring, a separately controlled ECU/main-relay supply, and an
independent ignition/key signal. Do not enable firmware main-relay control from
the measured battery voltage alone: depending on the wiring, that can create a
self-holding relay latch.

When a Burn disables EMP or changes its CAN bus/address after `0xF5` was queued,
the slow-control owner queues `0xF0` on the old endpoint and waits for the old
software TX queue to drain. Only then does it publish the new receive endpoint.
This preserves command ordering across a bus or address change.

## TunerStudio setup

1. Open **CAN Bus > EMP coolant pump**.
2. Confirm CAN read/write are enabled and the selected bus is configured for
   500 kbit/s.
3. Confirm controller and ECU source addresses.
4. Review RPM limits, CLT curve, minimum-flow curve, and failsafe RPM.
5. Review after-run thresholds and battery limits against the vehicle wiring.
6. Select curve or closed-loop operation and calibrate the control gains.
7. Enable EMP and Burn the configuration.
8. Verify target RPM, actual RPM, main-status age, capabilities, and faults
   before relying on closed-loop or after-run operation.

The service controls are intentionally bounded by the configured RPM and time.
Service start is rejected while the engine is running or cranking unless
`Allow service while engine runs` is explicitly enabled. Keep that override
disabled for normal vehicle use.

## Live data and diagnostics

TunerStudio exposes:

- operating and thermal state;
- target and measured pump RPM;
- controller status and status age;
- detected status-message capabilities;
- raw voltage/current, power, and external temperature;
- after-run time remaining;
- last command and control flags;
- filtered IAT, CLT error and CLT slope;
- minimum-flow, feed-forward, PI correction, cooling demand, and saturation
  time;
- latched, controller, sensor, timeout, capacity, and overload faults.

`EmpPumpFaultTx` is raised if the software TX queue fills. FOME's current CAN
transmit API does not return a per-frame completion result, so this fault does
not report hardware transmission failure. Global CAN failure counters are
shared with other modules and are not used as an EMP-specific approximation.
`reportTransmitResult()` remains as a future driver hook and for testing.

## Threading model

The slow engine callback is the sole owner of regulator state and CAN command
framing. It submits commands to a bounded queue per bus without waiting for a
CAN mailbox; each bus's CAN TX thread performs the potentially blocking send.
When a queue is full, the slow callback retries on its next pass. Configuration
changes, service commands, ignition changes, engine-stop notification, and
optional TX results arrive through atomic mailboxes.

CAN1 and CAN2 each have their own receive thread, so EMP maintains one
single-writer telemetry mailbox per bus. A received frame captures the active
endpoint and configuration generation and rechecks both before publishing.
Frames accepted just before a bus/address change therefore cannot populate the
new generation. Live status and the active configuration are published through
bounded sequence snapshots; no cross-priority spinlock is used and no CAN
transmission occurs on the main loop.

## Configuration migration

Adding EMP and the shock-preload configuration changes the persistent layout.
This branch uses one coordinated migration:

```text
TS_FILE_VERSION    = 20260910
FLASH_DATA_VERSION = 260910
```

Old flash data will be rejected because its version/size no longer matches.
Export the existing tune before flashing and import/review it afterward. The
generated Core8 and F407 TunerStudio INI files use a page size of 23836 bytes.

## Verification

Host tests cover extended CAN filtering and framing, safe service operation,
bootstrap without a Burn, configuration validation, Power Hold release on the
old endpoint, cross-bus stale-frame rejection, Status2/Status3/HVIL decoding,
external temperature and address claim, hot-boot after-run, battery hysteresis,
status timeout, minimum-flow/ramping, and fan-capacity protection.

Run the focused host suite with:

```sh
make -C unit_tests build/fome_test -j2
unit_tests/build/fome_test --gtest_filter='EmpPump.*:ShockPreload.*'
```

Build the Core8 firmware and OpenBLT image with:

```sh
bash firmware/config/boards/core8/compile_core8.sh
```

Before vehicle use, bench-test normal running, sensor failsafe, service stop,
engine stop, battery cutoff/resume, CAN loss, and explicit `0xF0` Power Hold
release with the real pump on a terminated 500 kbit/s bus.

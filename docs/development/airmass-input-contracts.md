# Captured air-model inputs and load coordinates

This preparation step follows the independent standalone tables. It supplies
the calculation contracts needed by SD + Alpha-N without enabling a composite
runtime mode. Standalone SD, Alpha-N and MAF retain their existing behavior.

## Acquisition and evaluation

`AirmassInputs` is caller-owned. It retains RPM, measured and effective MAP,
TPS, pedal, IAT, charge temperature, engine geometry and the relevant selectors.
Acquisition is sequential; it does not make asynchronous sensors sample at the
same instant. Calibration tables remain in configuration storage.

The raw SD and Alpha-N paths evaluate their own dedicated tables and existing
pressure/temperature equations. They do not apply the shared idle table or VE
corrections. Each result reports validity independently from its numeric value.
Optional diagnostics identify the table value actually used.

The common correction pass uses captured core inputs. Other configured GPPWM
channels are read once per distinct channel during that pass. Fuel-load and
ignition-load correction channels retain the previous published calculation.
No partial model result is published to supply a correction input.

Legacy evaluation keeps its existing correction order and numeric sensor
fallbacks. These fallbacks are not evidence of valid input for a composite.

## Load contract

Lambda and ignition choose their coordinates independently:

| Selector | Source | Units |
| --- | --- | --- |
| Default | Captured effective MAP | kPa |
| MAP | Captured measured MAP | kPa |
| TPS | Captured TPS1 | percent |
| Accelerator pedal | Captured pedal | percent |
| Cylinder filling | Final mass / standard cylinder charge | percent |

An accepted MAP estimate can satisfy effective MAP. It cannot satisfy an
explicit measured-MAP selector. Unused input failures do not invalidate a model
or load selector, but the planned composite retains effective MAP as its default
fueling load even at 100% Alpha-N.

MAF remains standalone: its native load is cylinder filling before its VE
correction, while normalized filling uses the corrected final mass.

## Existing load consumers

| Coordinate | Consumers |
| --- | --- |
| `fuelingLoad` | Injection phase, cylinder fuel trims, STFT regions, lambda monitoring, default VVT axis, trailing spark table and the GPPWM fuel-load channel. |
| `afrTableYAxis` | Lambda lookup and staged-injection fraction. |
| `ignitionLoad` | Main ignition advance and corrections, cylinder ignition trims, knock gain/retard and the GPPWM ignition-load channel. |
| `normalizedCylinderFilling` | Explicit cylinder-filling overrides, calculated from final mass. |

The OBD engine-load PID already packs `fuelingLoad` into a percent byte even
when standalone SD supplies kPa. The composite retains that existing behavior;
this does not make MAP a normalized torque or filling measurement.

The routing is in `fuel_math.cpp`, `fuel/fuel_computer.cpp`, `engine2.cpp`,
`closed_loop_fuel.cpp`, `actuators/vvt.cpp`, `actuators/gppwm/gppwm_channel.cpp`,
`engine_cycle/knock_controller.cpp` and `can/obd2.cpp`.

## Integration boundary

The owner must validate the composite configuration, authority and active input
dependencies, apply common corrections once, and resolve downstream loads before
publication. Injection readiness additionally requires complete per-cylinder
fuel publication and the fault/recovery policy. This API alone does not authorize
injection or establish engine calibration quality.

## Verification

On 2026-09-29, `make -j12` from `unit_tests/` passed. The targeted air-model,
fuel and Lua selection passed 56 tests; the full suite passed 747 tests in
138 suites. Five new tests cover source provenance, strict estimate validation
before interpolation, raw model equations, correction placement and sampling,
optional versus required IAT, and all five load coordinates.

No persistent configuration or output layout changes are introduced here.
ARM resource measurements follow once the composite uses these APIs in the
production fuel path. Host tests do not establish execution timing or engine
calibration accuracy.

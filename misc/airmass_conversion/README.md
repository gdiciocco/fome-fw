# Convert a legacy FOME airmass calibration

This tool prepares an MSQ for the optional dedicated standalone tables. It
preserves the original strategy, its complete 16 x 16 map and both axes. Other
new strategy maps receive the same placeholder defaults as firmware. Those
defaults are not an engine calibration.

The current target definition also includes blend authority and readiness
controls. Conversion writes a zero-authority table and all three readiness
declarations as false. It retains the original standalone strategy.

Requires Python 3 and the generated INI shipped with the target firmware.
Build the target board or use its firmware bundle to obtain this INI.
Use an original legacy MSQ backup, before any dedicated maps were added.

```sh
python3 misc/airmass_conversion/convert.py old-tune.msq converted-tune.msq \
  --ini firmware/tunerstudio/generated/fome_core8.ini
```

The output must be a new file. The source is never rewritten. Source and target
must name the same FOME board. The target INI must have the supported map types,
dimensions and scaling; conversion updates the MSQ signature and page size to
that definition. This is a named-parameter tune conversion, not a binary flash
migration or validation of unrelated hardware settings across firmware versions.

## Conversion rules

| Original strategy | Result |
| --- | --- |
| Speed Density | Keep `veTable`, `veLoadBins`, `veRpmBins`. |
| Alpha-N | Copy the full map and axes to `alphaNTable`, `alphaNTpsBins`, `alphaNRpmBins`. |
| MAF Air Charge | Copy the full map and axes to `mafTable`, `mafLoadBins`, `mafRpmBins`. |

All conversions retain the original `ve*` values and unrelated parameters.
Alpha-N and MAF users must prepare a calibrated SD map before selecting SD;
the retained shared map still contains their original strategy's calibration.

The output explicitly enables `useDedicatedAirmassTables` and sets
`veOverrideMode` to `None`. Alpha-N with an explicit TPS override is equivalent
and is accepted. Other overrides require separate calibration and are rejected.
In particular, an explicit MAP override may read measured MAP while the SD model
uses an estimated or transient MAP; those axes are not generally equivalent.

Values must fit the target storage without rounding; axes must be strictly
increasing and within their declared ranges. The converter rejects files with
any existing dedicated map fields, even if their opt-in is disabled, to avoid
overwriting a prepared calibration. Repeating conversion of the original backup
is deterministic. An already converted output is rejected.

Both new maps, authority, readiness controls and all axes are written explicitly. This prevents an import
from retaining stale values for absent new fields in an existing TS project.
Before importing an **unconverted** legacy MSQ into new firmware, disable
dedicated tables explicitly: absence of a key in the MSQ does not reset it.

## Restore and verify

1. Back up the original tune and project. The new firmware changes the flash
   format; it does not migrate the stored binary tune in place.
2. Generate the converted MSQ using the exact target-board INI.
3. With the engine stopped, install matching firmware/INI and import the output.
4. Check the strategy, opt-in, `None` VE override, all map values and axes, idle
   settings, lambda/ignition axes and unrelated hardware settings. Only the
   original strategy has been carried over as a calibration.
5. Burn, power cycle, and save a new MSQ. Compare the selected map, both axes and
   controls with the converted file before operating the engine.

The converter does not communicate with an ECU. TunerStudio import/burn/reload
and engine calibration must be checked separately; XML tests do not verify those
operations. Selecting SD + Alpha-N requires preparing both model maps and the
MAP-based downstream load tables, then explicitly declaring readiness. Conversion
does not perform that calibration or enable the composite mode.

## Regression tests

```sh
python3 -m unittest discover -s misc/airmass_conversion -p 'test_*.py' -v
```

The three checked-in MSQs are synthetic fixtures in the TunerStudio XML format.
Their asymmetric maps detect transposition and misplaced axes. Tests cover
preservation, repeated conversions, incompatible overrides, invalid values,
existing dedicated settings, mismatched boards/schema and exclusive output
creation. No private engine calibration is included.

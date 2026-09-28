#!/usr/bin/env python3
"""Convert a legacy, single-table FOME MSQ to dedicated standalone air maps."""

import argparse
import csv
from decimal import Decimal, InvalidOperation
from pathlib import Path
import re
import xml.etree.ElementTree as ET

NS = "http://www.msefi.com/:msq"
ET.register_namespace("", NS)
RPM = [650, 800, 1100, 1400, 1700, 2000, 2300, 2600,
       2900, 3200, 3500, 3800, 4100, 4400, 4700, 7000]
TPS = [0, 0.5, 1, 2, 3, 5, 7, 10, 15, 20, 30, 40, 55, 70, 85, 100]
MAF_LOAD = [0, 13, 27, 40, 53, 67, 80, 93, 107, 120, 133, 147, 160, 173, 187, 200]
NEW_FIELDS = {"alphaNTable", "alphaNTpsBins", "alphaNRpmBins",
              "mafTable", "mafLoadBins", "mafRpmBins",
              "airmassBlendTpsBins", "airmassBlendRpmBins", "airmassBlendTable",
              "sdAirmassMapReady", "alphaNAirmassMapReady", "mapEstimateReady"}
MODES = {"Speed Density": "sd", "MAF Air Charge": "maf", "Alpha-N": "alpha-n",
         "0": "sd", "1": "maf", "2": "alpha-n"}


class ConversionError(ValueError):
    pass


def number(text):
    try:
        value = Decimal(text)
    except InvalidOperation as exc:
        raise ConversionError(f"Invalid number: {text!r}") from exc
    if not value.is_finite():
        raise ConversionError(f"Nonfinite number: {text!r}")
    return value


def scale(text):
    parts = text.strip().strip("{}").split("/")
    if len(parts) == 1:
        return number(parts[0])
    if len(parts) == 2 and number(parts[1]) != 0:
        return number(parts[0]) / number(parts[1])
    raise ConversionError(f"Unsupported INI scale: {text}")


def board(signature):
    match = re.fullmatch(r"rusEFI \(FOME\) .+\.([\w-]+)\.\d+", signature)
    if not match:
        raise ConversionError("Expected a FOME firmware signature")
    return match[1]


def read_target(path):
    """Read only the single-page, fixed-layout declarations this conversion needs."""
    text = Path(path).read_text(encoding="utf-8")
    signature = re.search(r'^\s*signature\s*=\s*"([^"]+)"', text, re.M)
    size = re.search(r'^\s*pageSize\s*=\s*(\d+)\s*$', text, re.M)
    pages = re.search(r'^\s*nPages\s*=\s*1\s*$', text, re.M)
    if not signature or not size or not pages:
        raise ConversionError("Target INI must declare signature and single pageSize")
    declarations = {}
    for match in re.finditer(r"^\s*(\w+)\s*=\s*((?:array|bits),[^\n]+)", text, re.M):
        declarations[match[1]] = next(csv.reader([match[2]], skipinitialspace=True))
    expected = {
        "veTable": ("[16x16]", Decimal("0.1")),
        "veLoadBins": ("[16]", Decimal(1)),
        "veRpmBins": ("[16]", Decimal(1)),
        "alphaNTable": ("[16x16]", Decimal("0.1")),
        "alphaNTpsBins": ("[16]", Decimal("0.01")),
        "alphaNRpmBins": ("[16]", Decimal(1)),
        "mafTable": ("[16x16]", Decimal("0.1")),
        "mafLoadBins": ("[16]", Decimal(1)),
        "mafRpmBins": ("[16]", Decimal(1)),
        "airmassBlendTpsBins": ("[8]", Decimal("0.01")),
        "airmassBlendRpmBins": ("[8]", Decimal(1)),
        "airmassBlendTable": ("[8x8]", Decimal(1)),
    }
    occupied = []
    for name, (shape, multiplier) in expected.items():
        fields = [f.strip() for f in declarations.get(name, [])]
        data_type = "U08" if name == "airmassBlendTable" else "U16"
        if (len(fields) < 7 or fields[:2] != ["array", data_type]
                or fields[3] != shape or scale(fields[5]) != multiplier
                or number(fields[6]) != 0):
            raise ConversionError(f"Unsupported or missing target declaration: {name}")
        if not fields[2].isdigit():
            raise ConversionError(f"Invalid target offset: {name}")
        start = int(fields[2])
        count = 1
        for dimension in shape.strip("[]").split("x"):
            count *= int(dimension)
        end = start + count * (1 if data_type == "U08" else 2)
        if end > int(size[1]) or any(start < b and end > a for a, b in occupied):
            raise ConversionError(f"Target field overlaps or exceeds page bounds: {name}")
        occupied.append((start, end))
    readiness_offset = max(end for _, end in occupied)
    if readiness_offset + 4 != int(size[1]) or readiness_offset % 4:
        raise ConversionError("Unsupported readiness control layout")
    for bit, name in enumerate(("sdAirmassMapReady", "alphaNAirmassMapReady", "mapEstimateReady")):
        fields = [f.strip() for f in declarations.get(name, [])]
        if fields != ["bits", "U32", str(readiness_offset), f"[{bit}:{bit}]", "false", "true"]:
            raise ConversionError(f"Unsupported readiness declaration: {name}")
    opt_in = [f.strip() for f in declarations.get("useDedicatedAirmassTables", [])]
    if opt_in[:4] != ["bits", "U32", "580", "[5:5]"] or opt_in[4:] != ["false", "true"]:
        raise ConversionError("Target INI lacks the supported dedicated-table opt-in")
    board(signature[1])
    return signature[1], size[1]


def values(node, rows, cols, maximum, quantum):
    name = node.get("name")
    if node.get("rows") != str(rows) or node.get("cols") != str(cols):
        raise ConversionError(f"{name}: expected rows={rows}, cols={cols}")
    data = [number(token) for token in (node.text or "").split()]
    if len(data) != rows * cols:
        raise ConversionError(f"{name}: expected {rows * cols} values")
    step = Decimal(quantum)
    if any(v < 0 or v > maximum or v % step != 0 for v in data):
        raise ConversionError(f"{name}: values must fit 0..{maximum}, step {quantum}, without rounding")
    if cols == 1 and any(b <= a for a, b in zip(data, data[1:])):
        raise ConversionError(f"{name}: axis must be strictly increasing")
    return data


def scalar(node):
    return (node.text or "").strip().strip('"')


def add_array(page, tag, name, data, rows, cols, units, digits):
    node = ET.SubElement(page, tag, name=name, rows=str(rows), cols=str(cols),
                         units=units, digits=str(digits))
    node.text = "\n" + "\n".join(
        " ".join(str(v) for v in data[i:i + cols]) for i in range(0, len(data), cols)
    ) + "\n"


def convert(source, target_signature, target_size):
    if b"<!DOCTYPE" in source.upper() or b"<!ENTITY" in source.upper():
        raise ConversionError("MSQ document types and entities are unsupported")
    try:
        root = ET.fromstring(source)
    except ET.ParseError as exc:
        raise ConversionError(f"Invalid MSQ XML: {exc}") from exc
    if root.tag not in ("msq", f"{{{NS}}}msq"):
        raise ConversionError("Expected an MSQ document")
    prefix = f"{{{NS}}}" if root.tag.startswith("{") else ""
    version = root.find(prefix + "versionInfo")
    if version is None or version.get("nPages") != "1":
        raise ConversionError("Only single-page FOME MSQ files are supported")
    if board(version.get("signature", "")) != board(target_signature):
        raise ConversionError("Source MSQ and target INI must describe the same board")
    pages = [p for p in root.findall(prefix + "page") if p.get("number") is not None]
    if len(pages) != 1 or pages[0].get("number") != "0":
        raise ConversionError("Expected exactly one numbered page, number 0")
    page = pages[0]
    constants = {}
    for node in page.findall(prefix + "constant"):
        name = node.get("name")
        if not name or name in constants:
            raise ConversionError(f"Missing or duplicate constant name: {name}")
        constants[name] = node
    required = {"fuelAlgorithm", "veOverrideMode", "veTable", "veLoadBins", "veRpmBins"}
    if not required <= constants.keys():
        raise ConversionError(f"Missing legacy fields: {sorted(required - constants.keys())}")
    if NEW_FIELDS & constants.keys():
        raise ConversionError("Dedicated maps already exist; use the original legacy backup to avoid replacing calibration")
    opt_in = constants.get("useDedicatedAirmassTables")
    if opt_in is not None and scalar(opt_in) not in ("false", "0"):
        raise ConversionError("Dedicated mode is already enabled or has an invalid value")
    mode = MODES.get(scalar(constants["fuelAlgorithm"]))
    if mode is None:
        raise ConversionError("Only legacy SD, Alpha-N and MAF tunes can be converted")
    override = scalar(constants["veOverrideMode"])
    if override not in ("None", "0") and not (mode == "alpha-n" and override in ("TPS", "2")):
        raise ConversionError("The VE override differs from the model's natural axis; recalibration is required")

    old_map = values(constants["veTable"], 16, 16, 999, "0.1")
    old_rpm = values(constants["veRpmBins"], 16, 1, 18000, "1")
    old_load = values(constants["veLoadBins"], 16, 1, 100 if mode == "alpha-n" else 1000,
                      "0.01" if mode == "alpha-n" else "1")
    tag = prefix + "constant"
    add_array(page, tag, "alphaNTable", old_map if mode == "alpha-n" else [80] * 256, 16, 16, "%", 1)
    add_array(page, tag, "alphaNTpsBins", old_load if mode == "alpha-n" else TPS, 16, 1, "% TPS", 2)
    add_array(page, tag, "alphaNRpmBins", old_rpm if mode == "alpha-n" else RPM, 16, 1, "RPM", 0)
    add_array(page, tag, "mafTable", old_map if mode == "maf" else [100] * 256, 16, 16, "%", 1)
    add_array(page, tag, "mafLoadBins", old_load if mode == "maf" else MAF_LOAD, 16, 1, "% filling", 0)
    add_array(page, tag, "mafRpmBins", old_rpm if mode == "maf" else RPM, 16, 1, "RPM", 0)
    add_array(page, tag, "airmassBlendTpsBins", [0, 1, 3, 7, 15, 30, 60, 100], 8, 1, "% TPS", 2)
    add_array(page, tag, "airmassBlendRpmBins", [800, 1200, 2000, 3000, 4000, 5000, 6000, 8000], 8, 1, "RPM", 0)
    add_array(page, tag, "airmassBlendTable", [0] * 64, 8, 8, "% Alpha-N", 0)
    for name in ("sdAirmassMapReady", "alphaNAirmassMapReady", "mapEstimateReady"):
        ET.SubElement(page, tag, name=name).text = '"false"'
    if opt_in is None:
        opt_in = ET.SubElement(page, tag, name="useDedicatedAirmassTables")
    opt_in.text = '"true"'
    constants["veOverrideMode"].text = '"None"'
    if "unused580b5" in constants:
        page.remove(constants["unused580b5"])
    page.set("size", target_size)
    version.set("signature", target_signature)
    # Other metadata and every unrelated constant retain their original values.
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True), mode


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Original legacy MSQ backup")
    parser.add_argument("output", type=Path, help="New MSQ file; must not already exist")
    parser.add_argument("--ini", required=True, type=Path, help="Matching generated target-board INI")
    args = parser.parse_args(argv)
    try:
        signature, size = read_target(args.ini)
        result, mode = convert(args.source.read_bytes(), signature, size)
        with args.output.open("xb") as output:
            output.write(result)
    except (ConversionError, OSError) as exc:
        parser.exit(2, f"Conversion failed: {exc}\n")
    print(f"Converted {mode} to {args.output}; source unchanged.")
    print("Import only with the engine stopped and the matching firmware/INI installed.")
    print("Only the original strategy retains its calibration. Prepare other strategies before selecting them.")


if __name__ == "__main__":
    main()

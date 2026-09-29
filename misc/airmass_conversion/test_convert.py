import copy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

import convert

HERE = Path(__file__).parent
SIGNATURE = "rusEFI (FOME) feature/blended-airmass.2026.09.28.core8.123456"
NS = {"m": convert.NS}


def constants(payload):
    root = ET.fromstring(payload)
    return {c.get("name"): c for c in root.findall("m:page/m:constant", NS)}


def numbers(node):
    return [convert.number(v) for v in node.text.split()]


def target_ini(opt_in_bit=5):
    lines = [f'signature = "{SIGNATURE}"', "nPages = 1", "pageSize = 24988",
             f'useDedicatedAirmassTables = bits, U32, 580, [{opt_in_bit}:{opt_in_bit}], "false", "true"',
             'veLoadBins = array, U16, 17376, [16], "kPa", 1, 0, 0, 1000, 0',
             'veRpmBins = array, U16, 17408, [16], "RPM", 1, 0, 0, 18000, 0',
             'veTable = array, U16, 17440, [16x16], "%", 0.1, 0, 0, 999, 1']
    offset = 23736
    for name, shape, scale, count in [
        ("alphaNTpsBins", "[16]", "{1/100}", 16),
        ("alphaNRpmBins", "[16]", "1", 16),
        ("alphaNTable", "[16x16]", "0.1", 256),
        ("mafLoadBins", "[16]", "1", 16),
        ("mafRpmBins", "[16]", "1", 16),
        ("mafTable", "[16x16]", "0.1", 256),
    ]:
        lines.append(f'{name} = array, U16, {offset}, {shape}, "", {scale}, 0, 0, 1000, 1')
        offset += count * 2
    for name, shape, scale, count, dtype in [
        ("airmassBlendTpsBins", "[8]", "0.01", 8, "U16"),
        ("airmassBlendRpmBins", "[8]", "1", 8, "U16"),
        ("airmassBlendTable", "[8x8]", "1", 64, "U08"),
    ]:
        lines.append(f'{name} = array, {dtype}, {offset}, {shape}, "", {scale}, 0, 0, 100, 0')
        offset += count * (1 if dtype == "U08" else 2)
    for bit, name in enumerate(("sdAirmassMapReady", "alphaNAirmassMapReady", "mapEstimateReady")):
        lines.append(f'{name} = bits, U32, {offset}, [{bit}:{bit}], "false", "true"')
    return "\n".join(lines) + "\n"


class ConversionTest(unittest.TestCase):
    def fixture(self, strategy):
        return (HERE / "fixtures" / f"{strategy}.msq").read_bytes()

    def changed(self, name, text=None, **attributes):
        root = ET.fromstring(self.fixture("alpha-n"))
        node = root.find(f'm:page/m:constant[@name="{name}"]', NS)
        if text is not None:
            node.text = text
        node.attrib.update(attributes)
        return ET.tostring(root)

    def test_all_strategies_keep_legacy_values_and_copy_correct_map_without_transpose(self):
        for mode, target, load, rpm in [
            ("sd", "veTable", "veLoadBins", "veRpmBins"),
            ("alpha-n", "alphaNTable", "alphaNTpsBins", "alphaNRpmBins"),
            ("maf", "mafTable", "mafLoadBins", "mafRpmBins"),
        ]:
            with self.subTest(mode=mode):
                source = self.fixture(mode)
                result, selected = convert.convert(source, SIGNATURE, "24988")
                old, new = constants(source), constants(result)
                self.assertEqual(mode, selected)
                for name in ("veTable", "veLoadBins", "veRpmBins", "displacement", "fuelAlgorithm", "useSeparateVeForIdle"):
                    self.assertEqual(old[name].attrib, new[name].attrib)
                    self.assertEqual(old[name].text.strip(), new[name].text.strip())
                self.assertEqual(numbers(old["veTable"]), numbers(new[target]))
                self.assertEqual(numbers(old["veLoadBins"]), numbers(new[load]))
                self.assertEqual(numbers(old["veRpmBins"]), numbers(new[rpm]))
                self.assertEqual(numbers(new[target])[2 * 16 + 3], convert.number("44.3"))
                self.assertEqual(convert.scalar(new["useDedicatedAirmassTables"]), "true")
                self.assertEqual(convert.scalar(new["veOverrideMode"]), "None")
                self.assertNotIn("unused580b5", new)
                root = ET.fromstring(result)
                self.assertEqual(root.find("m:versionInfo", NS).get("signature"), SIGNATURE)
                self.assertEqual(root.find('m:page[@number="0"]', NS).get("size"), "24988")

    def test_complete_placeholder_maps_prevent_stale_import_fields(self):
        result, _ = convert.convert(self.fixture("sd"), SIGNATURE, "24988")
        data = constants(result)
        self.assertTrue(convert.NEW_FIELDS <= data.keys())
        self.assertEqual(numbers(data["alphaNTable"]), [80] * 256)
        self.assertEqual(numbers(data["mafTable"]), [100] * 256)
        self.assertEqual(numbers(data["airmassBlendTable"]), [0] * 64)
        self.assertEqual(numbers(data["airmassBlendTpsBins"]), [0, 1, 3, 7, 15, 30, 60, 100])
        self.assertEqual(numbers(data["airmassBlendRpmBins"]), [800, 1200, 2000, 3000, 4000, 5000, 6000, 8000])
        for name in ("sdAirmassMapReady", "alphaNAirmassMapReady", "mapEstimateReady"):
            self.assertEqual(convert.scalar(data[name]), "false")
        self.assertEqual(numbers(data["alphaNTpsBins"])[:4], [0, convert.number("0.5"), 1, 2])

    def test_equivalent_alpha_n_tps_override_becomes_natural_axis(self):
        result, _ = convert.convert(self.fixture("alpha-n"), SIGNATURE, "24988")
        self.assertEqual(convert.scalar(constants(result)["veOverrideMode"]), "None")

    def test_incompatible_overrides_are_rejected(self):
        for value in ('"MAP"', '"Unknown"'):
            with self.subTest(value=value), self.assertRaises(convert.ConversionError):
                convert.convert(self.changed("veOverrideMode", value), SIGNATURE, "24988")
        root = ET.fromstring(self.fixture("sd"))
        root.find('m:page/m:constant[@name="veOverrideMode"]', NS).text = '"MAP"'
        with self.assertRaises(convert.ConversionError):
            convert.convert(ET.tostring(root), SIGNATURE, "24988")

    def test_wrong_board_is_rejected(self):
        with self.assertRaisesRegex(convert.ConversionError, "same board"):
            convert.convert(self.fixture("sd"), SIGNATURE.replace("core8", "proteus_f7"), "28988")

    def test_duplicate_constant_is_rejected(self):
        root = ET.fromstring(self.fixture("sd"))
        page = root.find('m:page[@number="0"]', NS)
        page.append(copy.deepcopy(page[0]))
        with self.assertRaisesRegex(convert.ConversionError, "duplicate"):
            convert.convert(ET.tostring(root), SIGNATURE, "24988")

    def test_bad_dimensions_nonfinite_cells_and_rounding_are_rejected(self):
        for payload in [self.changed("veTable", rows="8"), self.changed("veTable", "NaN " * 256),
                        self.changed("veTable", "12.34 " * 256), self.changed("veTable", "9999 " * 256)]:
            with self.subTest(payload=payload[:30]), self.assertRaises(convert.ConversionError):
                convert.convert(payload, SIGNATURE, "24988")

    def test_axis_order_and_tps_range_are_checked(self):
        for axis in ["0 " * 16, " ".join(str(i * 10) for i in range(16)), "1 2 3"]:
            with self.subTest(axis=axis), self.assertRaises(convert.ConversionError):
                convert.convert(self.changed("veLoadBins", axis), SIGNATURE, "24988")

    def test_repeated_conversion_is_deterministic_and_converted_file_is_rejected(self):
        first, _ = convert.convert(self.fixture("alpha-n"), SIGNATURE, "24988")
        second, _ = convert.convert(self.fixture("alpha-n"), SIGNATURE, "24988")
        self.assertEqual(first, second)
        with self.assertRaisesRegex(convert.ConversionError, "already exist"):
            convert.convert(first, SIGNATURE, "24988")

    def test_existing_dedicated_calibration_is_not_overwritten_even_when_disabled(self):
        root = ET.fromstring(self.fixture("sd"))
        page = root.find('m:page[@number="0"]', NS)
        ET.SubElement(page, f"{{{convert.NS}}}constant", name="alphaNTable").text = "71"
        ET.SubElement(page, f"{{{convert.NS}}}constant", name="useDedicatedAirmassTables").text = '"false"'
        with self.assertRaisesRegex(convert.ConversionError, "already exist"):
            convert.convert(ET.tostring(root), SIGNATURE, "24988")

    def test_target_ini_schema_checks_shape_scale_and_opt_in(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "target.ini"
            path.write_text(target_ini())
            self.assertEqual(convert.read_target(path), (SIGNATURE, "24988"))
            for bad in [target_ini().replace("[16x16]", "[8x8]"),
                        target_ini().replace("{1/100}", "1"),
                        target_ini().replace("[5:5]", "[4:4]"),
                        target_ini().replace("veTable = array, U16", "veTable = array, U08"),
                        target_ini().replace("17408", "17376"),
                        target_ini().replace("pageSize = 24988", "pageSize = 24987"),
                        target_ini().replace("nPages = 1", "nPages = 2"),
                        target_ini().replace("airmassBlendTable = array, U08", "airmassBlendTable = array, U16"),
                        target_ini().replace("[2:2]", "[3:3]")]:
                path.write_text(bad)
                with self.assertRaises(convert.ConversionError):
                    convert.read_target(path)

    def test_cli_preserves_input_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            source, output, ini = folder / "old.msq", folder / "new.msq", folder / "target.ini"
            original = self.fixture("alpha-n")
            source.write_bytes(original)
            ini.write_text(target_ini())
            command = [sys.executable, str(HERE / "convert.py"), str(source), str(output), "--ini", str(ini)]
            success = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(success.returncode, 0, success.stderr)
            converted = output.read_bytes()
            refused = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(refused.returncode, 2)
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(output.read_bytes(), converted)

    def test_capoworks_bit26_preserves_named_flags_and_removes_reserved_aliases(self):
        with tempfile.TemporaryDirectory() as folder:
            ini = Path(folder) / "capoworks.ini"
            ini.write_text(target_ini(26) +
                           'enableShockPreload = bits, U32, 580, [5:5], "false", "true"\n' +
                           'enableEmpPump = bits, U32, 580, [6:6], "false", "true"\n')
            signature, size = convert.read_target(ini)
            for mode in ("sd", "alpha-n", "maf"):
                for shock, pump in (("true", "false"), ("false", "true")):
                    with self.subTest(mode=mode, shock=shock, pump=pump):
                        root = ET.fromstring(self.fixture(mode))
                        page = root.find('m:page[@number="0"]', NS)
                        preserved = {"enableShockPreload": f'"{shock}"',
                                     "enableEmpPump": f'"{pump}"',
                                     "shockPreloadCommandTarget": "37",
                                     "empPump_canBus": "1"}
                        for name, value in {**preserved, "unused580b26": '"true"'}.items():
                            ET.SubElement(page, f"{{{convert.NS}}}constant", name=name).text = value
                        source = ET.tostring(root)
                        result, selected = convert.convert(source, signature, size)
                        data = constants(result)
                        self.assertEqual(mode, selected)
                        for name, value in preserved.items():
                            self.assertEqual(value, data[name].text)
                        self.assertNotIn("unused580b5", data)
                        self.assertNotIn("unused580b26", data)
                        self.assertEqual("true", convert.scalar(data["useDedicatedAirmassTables"]))
                        self.assertEqual(numbers(constants(source)["veTable"]), numbers(data["veTable"]))

    def test_unknown_opt_in_bit_or_word_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            ini = Path(folder) / "unsupported.ini"
            layouts = [target_ini(bit) for bit in (4, 6, 25, 27)]
            layouts += [target_ini(26).replace("[26:26]", "[26:27]"),
                        target_ini(26).replace("bits, U32, 580,", "bits, U32, 584,")]
            for layout in layouts:
                with self.subTest(layout=layout.splitlines()[3]):
                    ini.write_text(layout)
                    with self.assertRaisesRegex(convert.ConversionError, "dedicated-table opt-in"):
                        convert.read_target(ini)


if __name__ == "__main__":
    unittest.main()

import ast
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "mb/tools"))
import grit_resource_ids as ids


class GritResourceIdsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.input = self.directory / "default_resource_ids"
        self.output = self.directory / "output" / "branding_resource_ids"
        self.receipt = self.directory / "output" / "receipt.json"
        self.mapping = {
            "SRCDIR": ".",
            "chrome/app/chromium_strings.grd": {"messages": [1234]},
            "components/components_chromium_strings.grd": {
                "META": {"join": 2, "sizes": {"messages": [19]}},
                "messages": [45678],
            },
        }
        self.write_input()

    def write_input(self):
        self.input.write_text(repr(self.mapping))

    def generate(self):
        ids.generate(self.input, self.directory, self.output, self.receipt)

    def test_copies_complete_resolved_allocations_and_records_exact_input(self):
        self.generate()
        actual = ast.literal_eval(self.output.read_text())
        self.assertEqual(actual["SRCDIR"], str(self.directory))
        for alias, original in ids.ALIASES.items():
            self.assertEqual(actual[alias], self.mapping[original])
        receipt = json.loads(self.receipt.read_text())
        self.assertEqual(receipt["input_sha256"], ids.digest(self.input.read_bytes()))
        self.assertEqual(receipt["output_sha256"], ids.digest(self.output.read_bytes()))
        self.assertEqual(receipt["resolved_allocations"], {
            original: self.mapping[original] for original in ids.ALIASES.values()})
        first = self.output.read_bytes(), self.receipt.read_bytes(), self.output.stat().st_mtime_ns
        self.generate()
        self.assertEqual(first, (self.output.read_bytes(), self.receipt.read_bytes(),
                                 self.output.stat().st_mtime_ns))
        self.mapping["components/components_chromium_strings.grd"]["messages"] = [56789]
        self.write_input()
        self.generate()
        actual = ast.literal_eval(self.output.read_text())
        self.assertEqual(actual["mb/generated/branding-strings/components_chromium_strings.grd"]
                         ["messages"], [56789])

    def test_rejects_executable_input_without_execution(self):
        marker = self.directory / "must-not-exist"
        self.input.write_text(f"__import__('pathlib').Path({str(marker)!r}).touch()")
        with self.assertRaisesRegex(ValueError, "invalid GRIT"):
            self.generate()
        self.assertFalse(marker.exists())
        self.assertFalse(self.output.exists())

    def test_rejects_wrong_source_root_and_missing_allocation(self):
        self.mapping["SRCDIR"] = "elsewhere"
        self.write_input()
        with self.assertRaisesRegex(ValueError, "SRCDIR"):
            self.generate()
        self.mapping["SRCDIR"] = "."
        del self.mapping["components/components_chromium_strings.grd"]
        self.write_input()
        with self.assertRaisesRegex(ValueError, "resolved messages allocation"):
            self.generate()
        self.assertFalse(self.output.exists())

    def test_refuses_symlinked_outputs_and_input_overwrite(self):
        target = self.directory / "existing"
        target.write_text("preserve")
        self.output.parent.mkdir()
        self.receipt.symlink_to(target)
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.generate()
        self.assertFalse(self.output.exists())
        self.assertEqual(target.read_text(), "preserve")
        with self.assertRaisesRegex(ValueError, "distinct"):
            ids.generate(self.input, self.directory, self.input, self.receipt)

    def test_actual_chromium_generated_map(self):
        source = ROOT / ".build/chromium/src"
        resolved_path = source / "out/mb-debug/gen/tools/gritsettings/default_resource_ids"
        if not resolved_path.is_file():
            self.skipTest("Chromium's generated default_resource_ids is required")
        ids.generate(resolved_path, source, self.output, self.receipt)
        resolved = ast.literal_eval(resolved_path.read_text())
        actual = ast.literal_eval(self.output.read_text())
        for alias, original in ids.ALIASES.items():
            self.assertEqual(actual[alias], resolved[original])
        receipt = json.loads(self.receipt.read_text())
        self.assertEqual(receipt["input_sha256"], ids.digest(resolved_path.read_bytes()))
        if not all((source / alias).is_file() for alias in ids.ALIASES):
            self.skipTest("Staged product string inputs are required for real GRIT comparison")
        sys.path.insert(0, str(source / "tools/grit"))
        from grit import grd_reader
        from grit.format import rc_header
        defines = {
            "_is_chrome_for_testing_branded": False,
            "toolkit_views": True,
            "use_titlecase": False,
            "enable_extensions": True,
            "enable_extensions_core": True,
            "enable_pdf": True,
        }
        for alias, original in ids.ALIASES.items():
            headers = []
            for filename, mapping in ((original, resolved_path), (alias, self.output)):
                document = grd_reader.Parse(str(source / filename),
                                            first_ids_file=str(mapping),
                                            defines=defines, target_platform="linux")
                document.SetOutputLanguage("en")
                document.InitializeIds()
                headers.append("".join(rc_header.Format(document)))
            self.assertEqual(headers[0], headers[1], original)


if __name__ == "__main__":
    unittest.main()

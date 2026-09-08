import copy
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "mb/tools"))
import branding
import branding_strings as strings

SOURCE = strings.DEFAULT_SOURCE
TMP = ROOT / ".build/tmp"


@unittest.skipUnless((SOURCE / "tools/grit/grit.py").is_file(), "pinned Chromium source required")
class BrandingStringsTest(unittest.TestCase):
    def setUp(self):
        TMP.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="branding-strings-", dir=TMP)
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.manifest = self.directory / "branding.toml"
        self.product = dict(branding.load_manifest(branding.DEFAULT_MANIFEST)["product"],
                            full_name="Orbit Browser", short_name="orbit")
        self.write_manifest()

    def write_manifest(self):
        self.manifest.write_text("schema_version = 1\n[product]\n" + "\n".join(
            f"{key} = {json.dumps(value)}" for key, value in self.product.items()) + "\n")

    def generate(self):
        return strings.generate(self.manifest, self.directory / "generated")

    def test_determinism_structure_and_attribution(self):
        output = self.generate()
        first = {p.name: p.read_bytes() for p in output.iterdir()}
        self.generate()
        self.assertEqual(first, {p.name: p.read_bytes() for p in output.iterdir()})
        receipt = json.loads(first[strings.RECEIPT])
        self.assertEqual(receipt["chromium_commit"], "d04cdb24d67b081f6cf80200ffc5233f44b61109")
        self.assertIn("chrome/app/resources/chromium_strings_de.xtb", receipt["source_sha256"])
        for relative in strings.SOURCES:
            original = strings.parse_xml((SOURCE / relative).read_bytes())
            derived = strings.parse_xml((output / Path(relative).name).read_bytes())
            if original.tag == "grit":
                self.assertEqual(ET.tostring(original.find("outputs")),
                                 ET.tostring(derived.find("outputs")))
            self.assertEqual([e.attrib for e in original.iter("if")],
                             [e.attrib for e in derived.iter("if")])
            old_messages, new_messages = list(original.iter("message")), list(derived.iter("message"))
            self.assertEqual(len(old_messages), len(new_messages))
            for old, new in zip(old_messages, new_messages):
                self.assertEqual(old.attrib, new.attrib)
                self.assertEqual(len(list(old.iter("ph"))), len(list(new.iter("ph"))))
                for old_ph, new_ph in zip(old.iter("ph"), new.iter("ph")):
                    old_ph, new_ph = copy.deepcopy(old_ph), copy.deepcopy(new_ph)
                    old_ph.tail = new_ph.tail = None
                    self.assertEqual(ET.tostring(old_ph), ET.tostring(new_ph))
                name = old.get("name")
                if name in strings.PROTECTED_IDS or "COPYRIGHT" in name:
                    self.assertEqual(ET.tostring(old), ET.tostring(new))
                if name in strings.LICENSE_IDS:
                    self.assertTrue(new.text.strip().startswith("Orbit Browser"))
                    self.assertEqual([p.tail for p in old], [p.tail for p in new])
            for node in derived.findall("./translations/file"):
                self.assertTrue(Path(node.get("path")).is_absolute())

    def test_refuses_formatting_characters(self):
        for character in "%{}#'&<>[]":
            with self.subTest(character=character):
                self.product["full_name"] = "Orbit" + character + "Browser"
                self.write_manifest()
                with self.assertRaisesRegex(branding.BrandingError, "product.full_name.*formatting"):
                    self.generate()
        self.assertFalse((self.directory / "generated").exists())

    def test_replaces_whole_words_outside_placeholder_subtrees(self):
        root = strings.parse_xml(b'<grit-part><message name="IDS_SAMPLE">Chromium ChromiumOS '
                                 b'<ph name="VALUE">$1<ex>Chromium</ex></ph> in Chromium.'
                                 b'</message></grit-part>')
        strings.transform(root, self.product)
        message = root.find("message")
        self.assertEqual(message.text, "Orbit Browser ChromiumOS ")
        self.assertEqual(message.find("ph/ex").text, "Chromium")
        self.assertEqual(message.find("ph").tail, " in Orbit Browser.")

    def test_refuses_unsafe_managed_output(self):
        output = self.generate()
        extra = output / "someone-elses-file"
        extra.write_text("keep")
        with self.assertRaisesRegex(branding.BrandingError, "unmanaged|unexpected"):
            self.generate()
        self.assertEqual(extra.read_text(), "keep")
        extra.unlink()
        target = output / "first_ids.py"
        target.write_text("edited")
        with self.assertRaisesRegex(branding.BrandingError, "modified"):
            self.generate()
        target.unlink()
        target.symlink_to(self.manifest)
        with self.assertRaisesRegex(branding.BrandingError, "unsafe"):
            self.generate()
        with self.assertRaisesRegex(branding.BrandingError, "inside Chromium"):
            strings.check_output(SOURCE / "mb-branding-test-must-not-exist", SOURCE.resolve())
        alias = self.directory / "alias"
        alias.symlink_to(output, target_is_directory=True)
        with self.assertRaisesRegex(branding.BrandingError, "symlink"):
            strings.check_output(alias, SOURCE.resolve())

    def test_refuses_malformed_receipt(self):
        output = self.generate()
        receipt = json.loads((output / strings.RECEIPT).read_text())
        receipt["outputs"] = sorted(strings.OUTPUT_NAMES)
        (output / strings.RECEIPT).write_text(json.dumps(receipt))
        with self.assertRaisesRegex(branding.BrandingError, "invalid managed output"):
            self.generate()

    def test_rejects_modified_input_even_at_matching_head(self):
        output = self.generate()
        before = {p.name: p.read_bytes() for p in output.iterdir()}
        receipt = json.loads(before[strings.RECEIPT])
        tree = b"".join(b"100644 blob " + value.encode() + b"\t" + path.encode() + b"\0"
                        for path, value in receipt["source_git_blobs"].items())
        original_read = Path.read_bytes
        modified_path = SOURCE.resolve() / "chrome/app/settings_chromium_strings.grdp"

        def read_with_local_edit(path):
            data = original_read(path)
            return data + b"\n<!-- local uncommitted edit -->\n" if path == modified_path else data

        def pinned_git(command, **kwargs):
            if "rev-parse" in command:
                return subprocess.CompletedProcess(command, 0, receipt["chromium_commit"] + "\n")
            self.assertIn("ls-tree", command)
            self.assertIn(receipt["chromium_commit"], command)
            self.assertEqual(set(command[command.index("--") + 1:]),
                             set(receipt["source_git_blobs"]))
            return subprocess.CompletedProcess(command, 0, tree)

        with mock.patch.object(Path, "read_bytes", read_with_local_edit), \
                mock.patch.object(strings.subprocess, "run", side_effect=pinned_git) as git:
            with self.assertRaisesRegex(branding.BrandingError,
                                        "input differs from pinned Git blob.*settings_chromium_strings"):
                self.generate()
            self.assertEqual(git.call_count, 2)
        self.assertEqual(before, {p.name: p.read_bytes() for p in output.iterdir()})

    def compile_bundle(self, directory, filename):
        root = strings.parse_xml((directory / filename).read_bytes())
        outputs = root.find("outputs")
        selected = [copy.deepcopy(node) for node in outputs.iter("output")
                    if node.get("type") == "rc_header" or
                    (node.get("type") == "data_package" and node.get("lang") in {"en", "de"})]
        outputs.clear()
        outputs.extend(selected)
        (directory / filename).write_text(strings.serialize(root))
        destination = directory / "paks"
        expected = directory / (filename + ".expected")
        expected.write_text("\n".join(str(destination / node.get("filename"))
                                      for node in selected) + "\n")
        command = [sys.executable, str(SOURCE / "tools/grit/grit.py"),
                   "-i", str(directory / filename), "build", "-o", str(destination),
                   "-f", str(directory / "first_ids.py"), "-t", "linux",
                   "--assert-file-list", str(expected)]
        for define in ["_is_chrome_for_testing_branded=false", "toolkit_views=true",
                       "use_titlecase=false", "enable_extensions=true",
                       "enable_extensions_core=true", "enable_pdf=true"]:
            command += ["-D", define]
        result = subprocess.run(command, capture_output=True, text=True, timeout=120,
                                env={**os.environ, "GRIT_DISABLE_MULTIPROCESSING": "1",
                                     "TMPDIR": str(TMP)})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        header = destination / next(n.get("filename") for n in selected if n.get("type") == "rc_header")
        ids = {name: int(value) for name, value in re.findall(
            r"^#define\s+(\w+)\s+(\d+)\s*$", header.read_text(), re.M)}
        self.assertGreater(len(ids), 10)
        sys.path.insert(0, str(SOURCE / "tools/grit"))
        from grit.format import data_pack
        paks = {n.get("lang"): data_pack.ReadDataPack(str(destination / n.get("filename"))).resources
                for n in selected if n.get("type") == "data_package"}
        return ids, paks

    def test_real_grit_ids_paks_and_translation_fallback(self):
        generated = self.generate()
        baseline, derived = self.directory / "baseline", self.directory / "derived"
        baseline.mkdir()
        derived.mkdir()
        for path in generated.iterdir():
            if path.name != strings.RECEIPT:
                (derived / path.name).write_bytes(path.read_bytes())
                (baseline / path.name).write_bytes(path.read_bytes())
        for relative in strings.SOURCES:
            root = strings.parse_xml((SOURCE / relative).read_bytes())
            for node in root.findall("./translations/file"):
                node.set("path", str((SOURCE / relative).parent / node.get("path")))
            (baseline / Path(relative).name).write_text(strings.serialize(root))
        for filename in ("chromium_strings.grd", "components_chromium_strings.grd"):
            with self.subTest(bundle=filename):
                old_ids, old_paks = self.compile_bundle(baseline, filename)
                new_ids, new_paks = self.compile_bundle(derived, filename)
                self.assertEqual(old_ids, new_ids)
                decode = lambda value: value.decode("utf-8")
                if filename == "chromium_strings.grd":
                    for name, expected in (("IDS_PRODUCT_NAME", "Orbit Browser"),
                                           ("IDS_SHORT_PRODUCT_NAME", "orbit")):
                        for locale in ("en", "de"):
                            self.assertEqual(decode(new_paks[locale][new_ids[name]]), expected)
                    for name in ("IDS_ABOUT_VERSION_COMPANY_NAME", "IDS_ABOUT_VERSION_COPYRIGHT"):
                        for locale in ("en", "de"):
                            self.assertEqual(old_paks[locale][old_ids[name]], new_paks[locale][new_ids[name]])
                    name = "IDS_FIRST_RUN_DIALOG_WINDOW_TITLE"
                    self.assertEqual(decode(new_paks["en"][new_ids[name]]), "Welcome to Orbit Browser")
                    self.assertEqual(new_paks["de"][new_ids[name]], new_paks["en"][new_ids[name]])
                else:
                    name = "IDS_VERSION_UI_LICENSE"
                    text = decode(new_paks["en"][new_ids[name]])
                    self.assertTrue(text.startswith("Orbit Browser is made possible"))
                    self.assertIn("Chromium", text)
                    self.assertIn("$1", text)
                    self.assertEqual(new_paks["de"][new_ids[name]], new_paks["en"][new_ids[name]])
                    self.assertNotEqual(new_paks["de"][new_ids[name]], old_paks["de"][old_ids[name]])
                # Find real German translations unchanged by the derivation,
                # not merely English text present in both locale packages.
                translated = [key for key in old_paks["de"]
                              if key in old_paks["en"] and key in new_paks["de"] and
                              old_paks["de"][key] != old_paks["en"][key] and
                              new_paks["de"][key] == old_paks["de"][key]]
                if filename == "chromium_strings.grd":
                    self.assertGreater(len(translated), 5)
                print(f"{filename}: {len(new_ids)} resource IDs unchanged; "
                      f"{len(new_paks['en'])} English pak entries; "
                      f"{len(translated)} unchanged German translations")


if __name__ == "__main__":
    unittest.main()

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "mb" / "tools"
sys.path.insert(0, str(TOOLS))
import branding  # noqa: E402


BASE_PRODUCT = {
    "short_name": "mb",
    "full_name": "mb",
    "executable_name": "mb",
    "profile_directory_name": "mb",
    "url_scheme": "mb",
    "description": "Matteo's Browser",
    "application_id": "com.matteo.mb",
    "desktop_file_name": "mb.desktop",
}


def manifest_text(product=BASE_PRODUCT, extra=""):
    lines = ["schema_version = 1", "", "[product]"]
    lines.extend(f"{key} = {json.dumps(value)}" for key, value in product.items())
    return "\n".join(lines) + "\n" + extra


class BrandingTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary_directory.name)
        self.manifest = self.directory / "branding.toml"
        self.manifest.write_text(manifest_text())

    def tearDown(self):
        self.temporary_directory.cleanup()

    def write_manifest(self, product=BASE_PRODUCT, extra=""):
        self.manifest.write_text(manifest_text(product, extra))

    def test_generates_deterministic_complete_outputs(self):
        first, second = self.directory / "one", self.directory / "two"
        branding.generate(self.manifest, first)
        branding.generate(self.manifest, second)
        self.assertEqual(
            {p.name: p.read_bytes() for p in first.iterdir()},
            {p.name: p.read_bytes() for p in second.iterdir()},
        )
        self.assertEqual(json.loads((first / "packaging.json").read_text())["application_id"], "com.matteo.mb")
        desktop = (first / "mb.desktop").read_text()
        self.assertIn('Exec="mb" %U', desktop)
        self.assertIn(
            "MimeType=text/html;application/xhtml+xml;x-scheme-handler/http;"
            "x-scheme-handler/https;x-scheme-handler/mb;",
            desktop,
        )
        self.assertIn("namespace mb::branding", (first / "branding.h").read_text())
        validator = shutil.which("desktop-file-validate")
        if validator:
            subprocess.run(
                [validator, str(first / "mb.desktop")],
                check=True,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

    def test_identity_is_manifest_derived_in_every_product_output(self):
        product = {
            "short_name": "orbit",
            "full_name": "Orbit Browser",
            "executable_name": "orbit",
            "profile_directory_name": "orbit",
            "url_scheme": "orbit",
            "description": "A browser for orbital work",
            "application_id": "org.example.orbit",
            "desktop_file_name": "orbit.desktop",
        }
        self.write_manifest(product)
        output = self.directory / "generated"
        branding.generate(self.manifest, output)
        for path in output.iterdir():
            if path.name in {branding.MANAGED_FILE, "branding.h"}:
                continue
            self.assertNotIn("mb", path.read_text().lower(), path.name)
        header = (output / "branding.h").read_text()
        self.assertIn('kFullName[] = "Orbit Browser"', header)
        self.assertIn("namespace mb::branding", header)  # Required stable API namespace.
        self.assertNotIn('= "mb"', header)

    def test_managed_output_accepts_a_new_manifest_desktop_filename(self):
        output = self.directory / "generated"
        branding.generate(self.manifest, output)
        product = dict(BASE_PRODUCT, desktop_file_name="orbit.desktop")
        self.write_manifest(product)
        branding.generate(self.manifest, output)
        self.assertTrue((output / "orbit.desktop").is_file())
        self.assertFalse((output / "mb.desktop").exists())

    def test_refuses_non_owned_desktop_filename_collision_without_writing(self):
        output = self.directory / "generated"
        branding.generate(self.manifest, output)
        protected = output / "orbit.desktop"
        protected.write_text("unrelated file")
        before = (output / "branding.h").read_bytes()
        self.write_manifest(dict(BASE_PRODUCT, desktop_file_name="orbit.desktop"))
        with self.assertRaisesRegex(branding.BrandingError, r"existing non-owned output.*orbit.desktop"):
            branding.generate(self.manifest, output)
        self.assertEqual(protected.read_text(), "unrelated file")
        self.assertEqual((output / "branding.h").read_bytes(), before)
        self.assertTrue((output / "mb.desktop").is_file())

    def test_refuses_unsafe_stale_output_before_writing(self):
        output = self.directory / "generated"
        branding.generate(self.manifest, output)
        stale = output / "mb.desktop"
        stale.unlink()
        stale.symlink_to(self.directory / "outside.desktop")
        before = (output / "branding.h").read_bytes()
        self.write_manifest(dict(BASE_PRODUCT, desktop_file_name="orbit.desktop"))
        with self.assertRaisesRegex(branding.BrandingError, r"unsafe managed output.*mb.desktop"):
            branding.generate(self.manifest, output)
        self.assertTrue(stale.is_symlink())
        self.assertFalse((output / "orbit.desktop").exists())
        self.assertEqual((output / "branding.h").read_bytes(), before)

    def test_gn_escapes_dollar_values_and_evaluates_them_when_gn_is_available(self):
        product = dict(
            BASE_PRODUCT,
            full_name="Literal $not_a_variable",
            description="A $literal description",
        )
        self.write_manifest(product)
        project = self.directory / "gn-project"
        branding.generate(self.manifest, project)
        generated = (project / "branding.gni").read_text()
        self.assertIn('branding_full_name = "Literal \\$not_a_variable"', generated)
        self.assertIn('branding_description = "A \\$literal description"', generated)
        (project / ".gn").write_text('buildconfig = "//BUILDCONFIG.gn"\n')
        (project / "BUILDCONFIG.gn").write_text('set_default_toolchain("//:default")\n')
        (project / "BUILD.gn").write_text(
            'import("//branding.gni")\n'
            'assert(branding_full_name == "Literal \\$not_a_variable")\n'
            'toolchain("default") {\n'
            '  tool("stamp") {\n'
            '    command = "touch {{output}}"\n'
            '  }\n'
            '}\n'
            'group("all") {}\n'
        )
        candidates = [
            ROOT / ".build/chromium/src/buildtools/linux64/gn",
            ROOT / ".build/depot_tools/gn",
        ]
        gn = next((str(path) for path in candidates if path.is_file()), None) or shutil.which("gn")
        if not gn:
            self.skipTest("no GN executable available")
        subprocess.run(
            [gn, "gen", str(project / "out"), f"--root={project}"],
            cwd=project,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def test_rejects_unknown_keys_and_wrong_types_with_key_diagnostics(self):
        self.manifest.write_text("schema_version = 1\nunexpected = true\n\n" + manifest_text().split("\n\n", 1)[1])
        with self.assertRaisesRegex(branding.BrandingError, r"manifest .*branding\.toml: manifest: unknown key\(s\): unexpected"):
            branding.load_manifest(self.manifest)
        product = dict(BASE_PRODUCT, url_scheme=3)
        self.write_manifest(product)
        with self.assertRaisesRegex(branding.BrandingError, r"manifest .*branding\.toml: product.url_scheme: must be a string"):
            branding.load_manifest(self.manifest)

    def test_reports_invalid_manifest_filename(self):
        missing = self.directory / "missing-branding.toml"
        with self.assertRaisesRegex(branding.BrandingError, r"missing-branding.toml"):
            branding.load_manifest(missing)

    def test_rejects_injection_controls_and_path_traversal(self):
        product = dict(BASE_PRODUCT, executable_name="../../outside")
        self.write_manifest(product)
        with self.assertRaisesRegex(branding.BrandingError, r"manifest .*branding\.toml: product.executable_name:.*path-safe"):
            branding.load_manifest(self.manifest)
        product = dict(BASE_PRODUCT, description="safe\n[Desktop Entry]\nExec=evil")
        self.write_manifest(product)
        with self.assertRaisesRegex(branding.BrandingError, r"manifest .*branding\.toml: product.description:.*control"):
            branding.load_manifest(self.manifest)
        product = dict(BASE_PRODUCT, desktop_file_name="../evil.desktop")
        self.write_manifest(product)
        with self.assertRaisesRegex(branding.BrandingError, r"manifest .*branding\.toml: product.desktop_file_name:.*path-safe"):
            branding.load_manifest(self.manifest)

    def test_refuses_unmanaged_or_chromium_source_output(self):
        unmanaged = self.directory / "unmanaged"
        unmanaged.mkdir()
        protected = unmanaged / "keep.txt"
        protected.write_text("do not overwrite")
        with self.assertRaisesRegex(branding.BrandingError, r"unmanaged"):
            branding.generate(self.manifest, unmanaged)
        self.assertEqual(protected.read_text(), "do not overwrite")
        with self.assertRaisesRegex(branding.BrandingError, r"chromium/src"):
            branding.generate(self.manifest, ROOT / ".build/chromium/src/branding-test")

    def test_generated_header_compiles_as_cxx20_when_a_compiler_is_available(self):
        compiler_candidates = [
            ROOT / ".build/chromium/src/third_party/llvm-build/Release+Asserts/bin/clang++",
            ROOT / ".build/chromium/src/third_party/llvm-build/Release/bin/clang++",
        ]
        compiler = next((str(path) for path in compiler_candidates if path.is_file()), None)
        compiler = compiler or shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            self.skipTest("no C++ compiler available")
        output = self.directory / "compiled"
        branding.generate(self.manifest, output)
        source = self.directory / "branding_compile_test.cc"
        source.write_text(
            '#include "branding.h"\n'
            'static_assert(mb::branding::kExecutableName[0] == \'m\');\n'
            "int main() { return mb::branding::kFullName[0] == 'm' ? 0 : 1; }\n"
        )
        subprocess.run(
            [compiler, "-std=c++20", "-I", str(output), "-c", str(source), "-o", str(self.directory / "test.o")],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )


if __name__ == "__main__":
    unittest.main()

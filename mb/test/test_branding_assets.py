import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "mb" / "tools"
sys.path.insert(0, str(TOOLS))
import branding  # noqa: E402
import branding_assets  # noqa: E402


def png_dimensions(contents):
    if contents[:8] != b"\x89PNG\r\n\x1a\n" or contents[12:16] != b"IHDR":
        raise AssertionError("not a PNG with IHDR")
    return struct.unpack(">II", contents[16:24])


class BrandingAssetsTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary_directory.name)
        self.manifest = ROOT / "mb" / "branding.toml"

    def tearDown(self):
        self.temporary_directory.cleanup()

    def test_generates_all_native_assets_with_expected_png_dimensions(self):
        output = self.directory / "assets"
        branding_assets.generate(self.manifest, output)
        expected = {}
        expected.update({f"unscaled/product_logo_{size}.png": size for size in branding_assets.UNSCALED_SIZES})
        expected.update({f"unscaled/linux/product_logo_{size}.png": size for size in branding_assets.LINUX_UNSCALED_SIZES})
        for scale, multiplier in (("default_100_percent", 1), ("default_200_percent", 2)):
            expected.update({f"{scale}/{name}": size * multiplier for name, size in branding_assets.SCALED_ASSETS.items()})
        self.assertEqual(set(branding_assets.asset_paths()), set(expected) | {
            "unscaled/product_logo.svg", "unscaled/product_logo_animation.svg",
        })
        for relative, size in expected.items():
            self.assertEqual(png_dimensions((output / relative).read_bytes()), (size, size), relative)
        receipt = json.loads((output / "receipt.json").read_text())
        self.assertIn("rsvg-convert version 2.62.3", receipt["renderer"]["version_output"])
        self.assertEqual(set(receipt["png_sha256"]), set(expected))

    def test_svg_variants_equal_branding_generator_original_icon(self):
        assets = self.directory / "assets"
        source = self.directory / "branding"
        branding_assets.generate(self.manifest, assets)
        branding.generate(self.manifest, source)
        original = (source / "icon.svg").read_bytes()
        self.assertEqual((assets / "unscaled/product_logo.svg").read_bytes(), original)
        self.assertEqual((assets / "unscaled/product_logo_animation.svg").read_bytes(), original)

    def test_repeat_generation_is_deterministic_in_this_environment(self):
        output = self.directory / "assets"
        branding_assets.generate(self.manifest, output)
        first = {path.relative_to(output): path.read_bytes() for path in output.rglob("*") if path.is_file()}
        branding_assets.generate(self.manifest, output)
        second = {path.relative_to(output): path.read_bytes() for path in output.rglob("*") if path.is_file()}
        self.assertEqual(first, second)

    def test_refuses_unmanaged_and_unsafe_managed_output_paths_before_writing(self):
        unmanaged = self.directory / "unmanaged"
        unmanaged.mkdir()
        protected = unmanaged / "keep.txt"
        protected.write_text("do not overwrite")
        with self.assertRaisesRegex(branding_assets.BrandingAssetsError, r"unmanaged"):
            branding_assets.generate(self.manifest, unmanaged)
        self.assertEqual(protected.read_text(), "do not overwrite")

        linked_output = self.directory / "linked-output"
        linked_output.symlink_to(unmanaged, target_is_directory=True)
        with self.assertRaisesRegex(branding_assets.BrandingAssetsError, r"symlinked output directory"):
            branding_assets.generate(self.manifest, linked_output)
        self.assertEqual(protected.read_text(), "do not overwrite")

        output = self.directory / "assets"
        branding_assets.generate(self.manifest, output)
        before = (output / "receipt.json").read_bytes()
        shutil.rmtree(output / "unscaled")
        (output / "unscaled").symlink_to(self.directory / "outside", target_is_directory=True)
        with self.assertRaisesRegex(branding_assets.BrandingAssetsError, r"unsafe output parent"):
            branding_assets.generate(self.manifest, output)
        self.assertTrue((output / "unscaled").is_symlink())
        self.assertEqual((output / "receipt.json").read_bytes(), before)


if __name__ == "__main__":
    unittest.main()

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


MODULE_PATH = Path(__file__).with_name("stage_release.py")
SPEC = importlib.util.spec_from_file_location("stage_release", MODULE_PATH)
STAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(STAGE)


class FakeProduct:
    def __init__(self, root, integration):
        self.SOURCE = root
        self.DEPOT = root / "depot"
        self.RECEIPT = root / "integration.json"
        self.RECEIPT.write_text(json.dumps(integration))
        self.integration = integration

    def verify(self):
        return self.integration

    def expected_product_binary(self, receipt, executable):
        output = self.SOURCE / "out/mb-release"
        if receipt.get("binary") != str(output / executable):
            raise RuntimeError("binary profile mismatch")
        return {"name": receipt.get("profile"), "output": output}, output / executable


class StageReleaseTest(unittest.TestCase):
    def fixture(self, root):
        output = root / "out/mb-release"
        (output / "locales").mkdir(parents=True)
        required = (
            "mb", "chrome_sandbox", "chrome_crashpad_handler",
            "chrome_management_service", "resources.pak", "icudtl.dat",
            "chrome_100_percent.pak", "chrome_200_percent.pak",
            "v8_context_snapshot.bin", "libvulkan.so.1",
            "libvk_swiftshader.so", "vk_swiftshader_icd.json",
        )
        for name in required:
            path = output / name
            path.write_bytes(name.encode())
            path.chmod(0o755)
        (output / "locales/en-US.pak").write_bytes(b"locale")
        credits = output / "gen/components/resources/about_credits.html"
        credits.parent.mkdir(parents=True)
        credits.write_text("credits")
        args = "is_debug = false\n"
        (output / "args.gn").write_text(args)
        (root / "LICENSE").write_text("license")
        (root / "mb").mkdir()
        (root / "mb/branding.toml").write_text(
            "[product]\nshort_name='mb'\nfull_name='mb'\n"
            "description=\"Matteo's Browser\"\nexecutable_name='mb'\n"
            "application_id='com.matteo.mb'\nprofile_directory_name='mb'\n"
            "desktop_file_name='mb.desktop'\nurl_scheme='mb'\n")
        generated = root / ".build/generated"
        (generated / "branding").mkdir(parents=True)
        (generated / "branding/mb.desktop").write_text("[Desktop Entry]\n")
        icons = generated / "branding-assets/unscaled"
        icons.mkdir(parents=True)
        for size in STAGE.ICON_SIZES:
            (icons / f"product_logo_{size}.png").write_bytes(b"icon")
        integration = {"product": {"executable_name": "mb"}}
        fake = FakeProduct(root, integration)
        receipt = root / "receipt.json"
        receipt.write_text(json.dumps({
            "stage": "build", "exit_code": 0, "profile": "release",
            "binary": str(output / "mb"),
            "binary_sha256": STAGE.file_sha(output / "mb"),
            "args_gn": args, "integration": integration,
            "integration_sha256": STAGE.file_sha(fake.RECEIPT),
        }))
        return output, receipt, fake

    def test_stages_identity_resources_and_setuid_sandbox(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output, receipt, fake = self.fixture(root)
            old_load = STAGE.load_product_module
            old_args = STAGE.resolved_args
            STAGE.load_product_module = lambda unused: fake
            STAGE.resolved_args = lambda unused_product, unused_output: {
                "use_static_angle": True,
                "angle_shared_libvulkan": True,
                "enable_swiftshader": True,
            }
            try:
                result = STAGE.stage(root, output, receipt, root / "pkg")
            finally:
                STAGE.load_product_module = old_load
                STAGE.resolved_args = old_args
            self.assertEqual(result["sandbox_mode"], "0o4755")
            self.assertTrue((root / "pkg/opt/mb/mb").is_file())
            self.assertTrue((root / "pkg/usr/bin/mb").is_symlink())
            self.assertTrue((root / "pkg/usr/share/applications/mb.desktop").is_file())

    def test_rejects_debug_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output, receipt, fake = self.fixture(root)
            data = json.loads(receipt.read_text())
            data["profile"] = "debug"
            receipt.write_text(json.dumps(data))
            old_load = STAGE.load_product_module
            STAGE.load_product_module = lambda unused: fake
            try:
                with self.assertRaisesRegex(RuntimeError, "release"):
                    STAGE.validate_receipt(
                        root, output, receipt,
                        STAGE.load_manifest(root / "mb/branding.toml"))
            finally:
                STAGE.load_product_module = old_load

    def test_missing_enabled_swiftshader_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output, unused, unused_fake = self.fixture(root)
            (output / "libvk_swiftshader.so").unlink()
            with self.assertRaisesRegex(RuntimeError, "libvk_swiftshader"):
                STAGE.payload(output, root / "LICENSE",
                              STAGE.load_manifest(root / "mb/branding.toml"), {
                    "use_static_angle": True,
                    "angle_shared_libvulkan": True,
                    "enable_swiftshader": True,
                })


if __name__ == "__main__":
    unittest.main()

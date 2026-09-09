import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "startup_config_gate.py"
SPEC = importlib.util.spec_from_file_location("startup_config_gate", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class FakeProductAPI:
    def __init__(self, root, integration):
        self.root = root
        self.integration = integration
        self.args = {"debug": "debug args\n", "release": "release args\n"}

    def verify(self):
        return self.integration

    def profile_from_build_receipt(self, receipt):
        profile = receipt.get("profile", "debug")
        if profile not in self.args or receipt.get("args_gn") != self.args[profile]:
            raise RuntimeError("receipt does not match product profile")
        return {"output_directory": f"out/mb-{profile}"}

    def expected_product_binary(self, receipt, executable_name):
        profile = self.profile_from_build_receipt(receipt)
        binary = self.root / ".build/chromium/src" / profile["output_directory"] / executable_name
        if receipt.get("binary") != str(binary):
            raise RuntimeError("receipt binary does not match product profile")
        return profile, binary


class StartupConfigGateHarnessTest(unittest.TestCase):
    def test_cases_are_bounded_and_use_only_fixed_expected_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            items = MODULE.cases(Path(directory))
        self.assertEqual(
            [item["name"] for item in items],
            [
                "invalid_toml",
                "unknown_environment",
                "malformed_unselected_url",
                "mismatched_user_data_dir",
                "rejected_user_data_environment",
                "duplicate_environment_selector",
                "missing_environment_selector",
                "remote_pipe_missing_fds",
            ],
        )
        self.assertTrue(all(item["args"] for item in items))
        self.assertEqual(items[2]["args"][-2:], ["--environment", "personal"])
        self.assertIn("--remote-debugging-pipe", items[-1]["args"])
        self.assertNotEqual(items[2]["expected_roots"], items[0]["expected_roots"])
        self.assertEqual(items[2]["expected_roots"][0].name, "personal-url")
        self.assertIn(Path("wrong"),
                      [root.relative_to(items[3]["expected_roots"][0].parent)
                       for root in items[3]["expected_roots"]])
        self.assertIn(Path("override"),
                      [root.relative_to(items[4]["expected_roots"][0].parent)
                       for root in items[4]["expected_roots"]])
        self.assertEqual(items[2]["expected_exit_code"], MODULE.UNSUPPORTED_PARAM_EXIT)

    def test_receipt_requires_product_binary_hash_and_arguments(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / ".build/chromium/src/out/mb-debug"
            output.mkdir(parents=True)
            binary = output / "mb"
            binary.write_bytes(b"product")
            binary.chmod(0o700)
            args_gn = "debug args\n"
            (output / "args.gn").write_text(args_gn)
            integration = {"product": {"executable_name": "mb"}}
            (root / ".build/product-integration.json").write_text(json.dumps(integration))
            receipt = root / "receipt.json"
            receipt.write_text(json.dumps({
                "stage": "build",
                "exit_code": 0,
                "binary": str(binary),
                "binary_sha256": MODULE.sha256(binary),
                "profile": "debug",
                "output_directory": "out/mb-debug",
                "args_gn": args_gn,
                "integration": integration,
                "integration_sha256": MODULE.sha256(
                    root / ".build/product-integration.json"),
            }))
            receipt_data = json.loads(receipt.read_text())
            receipt_data["integration_sha256"] = MODULE.sha256(
                root / ".build/product-integration.json")
            receipt.write_text(json.dumps(receipt_data))
            _, actual = MODULE.validate_receipt(
                receipt, root, verifier=lambda: integration,
                product_api=FakeProductAPI(root, integration))
            self.assertEqual(actual, binary.resolve())
            binary.write_bytes(b"changed")
            with self.assertRaisesRegex(RuntimeError, "differs"):
                MODULE.validate_receipt(
                    receipt, root, verifier=lambda: integration,
                    product_api=FakeProductAPI(root, integration))

    def test_release_profile_is_accepted_and_cross_profile_args_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / ".build/chromium/src/out/mb-release"
            output.mkdir(parents=True)
            binary = output / "mb"
            binary.write_bytes(b"release product")
            binary.chmod(0o700)
            api = FakeProductAPI(root, {"product": {"executable_name": "mb"}})
            args_gn = api.args["release"]
            (output / "args.gn").write_text(args_gn)
            integration = api.integration
            (root / ".build/product-integration.json").write_text(json.dumps(integration))
            receipt_data = {
                "stage": "build", "exit_code": 0, "binary": str(binary),
                "binary_sha256": MODULE.sha256(binary), "profile": "release",
                "output_directory": "out/mb-release", "args_gn": args_gn,
                "integration": integration,
                "integration_sha256": MODULE.sha256(root / ".build/product-integration.json"),
            }
            receipt = root / "release.json"
            receipt.write_text(json.dumps(receipt_data))
            _, actual = MODULE.validate_receipt(
                receipt, root, verifier=api.verify, product_api=api)
            self.assertEqual(actual, binary.resolve())
            receipt_data["args_gn"] = api.args["debug"]
            receipt.write_text(json.dumps(receipt_data))
            with self.assertRaisesRegex(RuntimeError, "profile"):
                MODULE.validate_receipt(receipt, root, verifier=api.verify, product_api=api)

    def test_wrong_root_and_crash_code_cannot_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "selected"
            with self.assertRaisesRegex(RuntimeError, "roots were created"):
                root.mkdir(parents=True)
                MODULE.assert_roots_absent([root, Path(directory) / "work"])
        with self.assertRaisesRegex(RuntimeError, "expected exit"):
            MODULE.validate_exit_code(139, MODULE.UNSUPPORTED_PARAM_EXIT)
        MODULE.validate_exit_code(MODULE.UNSUPPORTED_PARAM_EXIT,
                                  MODULE.UNSUPPORTED_PARAM_EXIT)

    def test_timeout_is_bounded(self):
        self.assertEqual(MODULE.MAX_TIMEOUT, 60)

    def test_cleanup_never_signals_an_already_reaped_leader(self):
        process = mock.Mock(pid=12345)
        with mock.patch.object(MODULE.os, "waitid", side_effect=ChildProcessError()), \
             mock.patch.object(MODULE.os, "killpg") as killpg:
            cleanup = MODULE.terminate_group(process)
        self.assertFalse(cleanup["owned"])
        self.assertFalse(cleanup["leader_reaped"])
        killpg.assert_not_called()


if __name__ == "__main__":
    unittest.main()


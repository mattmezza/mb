import hashlib
from pathlib import Path
import sys
import unittest


TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import browser_tests  # noqa: E402
import product  # noqa: E402


class ProductProfileTest(unittest.TestCase):
    executable_name = "product-browser"

    def receipt(self, name):
        profile = product.product_profile(name)
        args = profile["args_file"].read_text()
        return {
            "stage": "build",
            "exit_code": 0,
            "profile": name,
            "output_directory": profile["output_directory"],
            "args_gn": args,
            "args_gn_sha256": hashlib.sha256(args.encode()).hexdigest(),
            "binary": str(profile["output"] / self.executable_name),
        }

    def test_closed_profiles_have_distinct_fixed_outputs(self):
        debug = product.product_profile("debug")
        release = product.product_profile("release")
        self.assertEqual(debug["output_directory"], "out/mb-debug")
        self.assertEqual(release["output_directory"], "out/mb-release")
        self.assertNotEqual(debug["args_file"].read_text(), release["args_file"].read_text())
        with self.assertRaisesRegex(RuntimeError, "Unknown product build profile"):
            product.product_profile("external")

    def test_release_receipt_resolves_only_release_binary(self):
        receipt = self.receipt("release")
        profile, binary = product.expected_product_binary(receipt, self.executable_name)
        self.assertEqual(profile["name"], "release")
        self.assertEqual(binary, product.product_profile("release")["output"] / self.executable_name)

    def test_receipt_cannot_combine_debug_profile_with_release_output(self):
        receipt = self.receipt("debug")
        receipt["output_directory"] = "out/mb-release"
        receipt["binary"] = str(product.product_profile("release")["output"] / self.executable_name)
        with self.assertRaisesRegex(RuntimeError, "output directory"):
            product.expected_product_binary(receipt, self.executable_name)

    def test_receipt_cannot_combine_release_profile_with_debug_arguments(self):
        receipt = self.receipt("release")
        debug_args = product.product_profile("debug")["args_file"].read_text()
        receipt["args_gn"] = debug_args
        receipt["args_gn_sha256"] = hashlib.sha256(debug_args.encode()).hexdigest()
        with self.assertRaisesRegex(RuntimeError, "profile arguments"):
            product.expected_product_binary(receipt, self.executable_name)

    def test_new_receipt_cannot_point_at_another_profile_binary(self):
        receipt = self.receipt("release")
        receipt["binary"] = str(product.product_profile("debug")["output"] / self.executable_name)
        with self.assertRaisesRegex(RuntimeError, "binary.*profile output"):
            product.expected_product_binary(receipt, self.executable_name)

    def test_modern_receipt_requires_exact_arguments_hash(self):
        receipt = self.receipt("debug")
        del receipt["args_gn_sha256"]
        with self.assertRaisesRegex(RuntimeError, "arguments hash"):
            product.expected_product_binary(receipt, self.executable_name)
        receipt = self.receipt("debug")
        receipt["args_gn_sha256"] = "0" * 64
        with self.assertRaisesRegex(RuntimeError, "arguments hash"):
            product.expected_product_binary(receipt, self.executable_name)

    def test_partial_profile_metadata_is_not_legacy(self):
        debug = product.product_profile("debug")
        receipt = {
            "args_gn": debug["args_file"].read_text(),
            "output_directory": debug["output_directory"],
            "binary": str(debug["output"] / self.executable_name),
        }
        with self.assertRaisesRegex(RuntimeError, "partial profile metadata"):
            product.expected_product_binary(receipt, self.executable_name)

    def test_browser_test_binary_uses_the_receipt_profile(self):
        receipt = self.receipt("release")
        release = product.product_profile("release")
        receipt["test_binaries"] = {
            "mb_browser_tests": {
                "path": str(release["output"] / "mb_browser_tests"),
                "sha256": "a" * 64,
            }
        }
        profile, binary, digest = browser_tests.test_binary_from_receipt(receipt)
        self.assertEqual(profile["name"], "release")
        self.assertEqual(binary, release["output"] / "mb_browser_tests")
        self.assertEqual(digest, "a" * 64)

    def test_browser_test_binary_rejects_cross_profile_output(self):
        receipt = self.receipt("debug")
        receipt["test_binaries"] = {
            "mb_browser_tests": {
                "path": str(product.product_profile("release")["output"] / "mb_browser_tests"),
                "sha256": "a" * 64,
            }
        }
        with self.assertRaisesRegex(RuntimeError, "not the product mb_browser_tests output"):
            browser_tests.test_binary_from_receipt(receipt)

    def test_legacy_debug_browser_test_receipt_remains_valid(self):
        debug = product.product_profile("debug")
        receipt = {
            "args_gn": debug["args_file"].read_text(),
            "test_binaries": {
                "mb_browser_tests": {
                    "path": str(debug["output"] / "mb_browser_tests"),
                    "sha256": "a" * 64,
                }
            },
        }
        profile, binary, _ = browser_tests.test_binary_from_receipt(receipt)
        self.assertEqual(profile["name"], "debug")
        self.assertEqual(binary, debug["output"] / "mb_browser_tests")

    def test_legacy_debug_receipt_still_accepts_exact_debug_arguments(self):
        debug = product.product_profile("debug")
        receipt = {
            "args_gn": debug["args_file"].read_text(),
            "binary": str(debug["output"] / self.executable_name),
        }
        profile, binary = product.expected_product_binary(receipt, self.executable_name)
        self.assertEqual(profile["name"], "debug")
        self.assertEqual(binary, debug["output"] / self.executable_name)


if __name__ == "__main__":
    unittest.main()

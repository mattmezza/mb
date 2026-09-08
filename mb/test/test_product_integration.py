import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import product  # noqa: E402


class ProductIntegrationTest(unittest.TestCase):
    def test_safe_relative_rejects_traversal_and_backslashes(self):
        for name in ("", ".", "a/../b", "../outside", "/absolute", "a\\b", "a//b"):
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                product.safe_relative(name)

    def test_regular_destination_rejects_symlink_ancestors(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "source"
            target = Path(directory) / "target"
            root.mkdir()
            target.mkdir()
            (root / "chrome-link").symlink_to(target, target_is_directory=True)
            with self.assertRaisesRegex(RuntimeError, "symlink"):
                product.regular_destination(root, "chrome-link/file.txt")

    def test_check_changes_refuses_unrecognized_local_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            path = source / "chrome" / "browser.cc"
            path.parent.mkdir()
            path.write_text("local edit\n")
            with mock.patch.object(product, "SOURCE", source), mock.patch.object(
                product.upstream, "git", side_effect=("", "")
            ):
                with self.assertRaisesRegex(RuntimeError, "Local edit preserved"):
                    product.check_changes(
                        {"chrome/browser.cc": b"generated\n"},
                        {"chrome/browser.cc": b"upstream\n"},
                        {},
                    )

    def test_check_changes_accepts_exact_previous_content(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            path = source / "chrome" / "browser.cc"
            path.parent.mkdir()
            path.write_bytes(b"already staged\n")
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            with mock.patch.object(product, "SOURCE", source), mock.patch.object(
                product.upstream, "git", side_effect=("", "")
            ):
                product.check_changes({}, {}, {"chrome/browser.cc": digest})

    def test_patch_payload_applies_only_existing_scoped_text_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            patch_dir = root / "mb" / "patches"
            patch_dir.mkdir(parents=True)
            patch = patch_dir / "0001-test.patch"
            patch.write_text(
                "diff --git a/chrome/browser.cc b/chrome/browser.cc\n"
                "--- a/chrome/browser.cc\n"
                "+++ b/chrome/browser.cc\n"
                "@@ -1 +1 @@\n"
                "-upstream\n"
                "+product\n"
            )
            with mock.patch.object(product, "ROOT", root), mock.patch.object(
                product, "BUILD", root / "build"
            ), mock.patch.object(product, "SOURCE", source), mock.patch.object(
                product.subprocess, "check_output", return_value=b"upstream\n"
            ):
                expected, original, hashes = product.patch_payload()
            self.assertEqual(original, {"chrome/browser.cc": b"upstream\n"})
            self.assertEqual(expected, {"chrome/browser.cc": b"product\n"})
            self.assertEqual(hashes["mb/patches/0001-test.patch"], product.sha(patch.read_bytes()))

    def test_retired_payload_restores_tracked_and_classifies_generated_files(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            source.mkdir()
            tracked = source / "chrome" / "retired.cc"
            tracked.parent.mkdir()
            tracked.write_text("pinned upstream\n")
            subprocess.run(["git", "-C", str(source), "init", "-q"], check=True)
            subprocess.run(["git", "-C", str(source), "config", "user.email", "test@example.invalid"], check=True)
            subprocess.run(["git", "-C", str(source), "config", "user.name", "Fixture Test"], check=True)
            subprocess.run(["git", "-C", str(source), "add", "chrome/retired.cc"], check=True)
            subprocess.run(["git", "-C", str(source), "commit", "-qm", "pinned"], check=True)
            previous = {
                "chrome/retired.cc": "old-patch-hash",
                "mb/generated/retired.txt": "generated-hash",
            }
            with mock.patch.object(product, "SOURCE", source):
                restores, generated_retired = product.retired_payload(previous, {})
            self.assertEqual(restores, {"chrome/retired.cc": b"pinned upstream\n"})
            self.assertEqual(generated_retired, {"mb/generated/retired.txt"})

    def test_verify_product_sources_detects_deleted_source_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            managed = root / "mb" / "managed.txt"
            generated = root / "mb" / "generated" / "managed.txt"
            generated.parent.mkdir(parents=True)
            managed.write_text("managed\n")
            generated.write_text("generated\n")
            files = {
                "mb/managed.txt": product.sha(managed.read_bytes()),
                "mb/generated/managed.txt": product.sha(generated.read_bytes()),
            }
            receipt = {"files": files, "source_files": sorted(files)}
            with mock.patch.object(product, "ROOT", root):
                product.verify_product_sources(receipt)
                managed.unlink()
                with self.assertRaisesRegex(RuntimeError, "file set changed"):
                    product.verify_product_sources(receipt)

    def test_verify_gate_requires_reviewed_success_and_pinned_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = root / "evidence"
            evidence.mkdir()
            screenshot = evidence / "startup.png"
            screenshot.write_bytes(b"screenshot")
            (evidence / "results.md").write_text("reviewed\n")
            binary = root / "source" / "out" / "mb-debug" / "chrome"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"binary")
            binary_hash = product.file_sha(binary)
            receipt_path = root / "build.json"
            receipt_path.write_text(json.dumps({
                "stage": "build",
                "exit_code": 0,
                "chromium_commit": "chromium-pin",
                "depot_tools_commit": "depot-pin",
                "binary_sha256": binary_hash,
            }))
            review_path = root / "review.json"
            review_path.write_text(json.dumps({
                "schema_version": 1,
                "result": "passed",
                "evidence_directory": str(evidence),
                "review_sha256": product.file_sha(evidence / "results.md"),
                "screenshots": {"startup.png": product.file_sha(screenshot)},
                "build_receipt": str(receipt_path),
                "binary_sha256": binary_hash,
            }))
            (evidence / "results.json").write_text(json.dumps({
                "automated_result": "passed",
                "browser_exit_code": 0,
                "binary_sha256": binary_hash,
            }))
            pins = {"chromium": {"commit": "chromium-pin"}, "depot_tools": {"commit": "depot-pin"}}
            with mock.patch.object(product, "SOURCE", root / "source"):
                self.assertEqual(product.verify_gate(review_path, pins)["binary_sha256"], binary_hash)
                receipt_path.write_text(json.dumps({
                    "stage": "build", "exit_code": 0,
                    "chromium_commit": "wrong", "depot_tools_commit": "depot-pin",
                    "binary_sha256": binary_hash,
                }))
                with self.assertRaisesRegex(RuntimeError, "differs from pin"):
                    product.verify_gate(review_path, pins)

            review = json.loads(review_path.read_text())
            review["result"] = "pending"
            review_path.write_text(json.dumps(review))
            with self.assertRaisesRegex(RuntimeError, "visually reviewed"):
                product.verify_gate(review_path, pins)


if __name__ == "__main__":
    unittest.main()

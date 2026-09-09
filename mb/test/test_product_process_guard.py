import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import product  # noqa: E402


class ProductProcessGuardTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.proc = self.root / "proc"
        self.proc.mkdir()
        self.debug = self.root / "out" / "mb-debug"
        self.release = self.root / "out" / "mb-release"
        self.outside = self.root / "outside"
        for directory in (self.debug, self.release, self.outside):
            directory.mkdir(parents=True)

    def tearDown(self):
        self.temporary_directory.cleanup()

    def process(self, pid, target):
        directory = self.proc / str(pid)
        directory.mkdir()
        (directory / "exe").symlink_to(target)

    def live(self, *, uid=None):
        return product.live_product_output_processes(
            self.proc,
            uid=os.getuid() if uid is None else uid,
            output_directories=(self.debug.resolve(), self.release.resolve()),
        )

    def test_finds_same_user_debug_release_and_deleted_executables(self):
        self.process(101, self.debug / "chrome")
        self.process(102, self.release / "chrome-sandbox")
        self.process(103, self.debug / "chrome (deleted)")
        self.process(104, self.outside / "python")
        self.assertEqual(
            self.live(),
            [(101, str(self.debug / "chrome")),
             (102, str(self.release / "chrome-sandbox")),
             (103, str(self.debug / "chrome"))],
        )

    def test_ignores_other_users_and_vanished_entries(self):
        self.process(201, self.debug / "chrome")
        (self.proc / "202").mkdir()
        self.assertEqual(self.live(uid=os.getuid() + 1), [])
        self.assertEqual(self.live(), [(201, str(self.debug / "chrome"))])

    def test_permission_to_read_an_unidentified_executable_does_not_block(self):
        self.process(301, self.debug / "chrome")
        original_readlink = os.readlink

        def denied(path):
            if Path(path).name == "exe":
                raise PermissionError("denied")
            return original_readlink(path)

        with mock.patch.object(product.os, "readlink", side_effect=denied):
            self.assertEqual(self.live(), [])

    def test_canonical_symlink_target_is_detected(self):
        alias = self.root / "alias"
        alias.symlink_to(self.debug, target_is_directory=True)
        self.process(401, alias / "chrome")
        self.assertEqual(self.live(), [(401, str(alias / "chrome"))])

    def test_unavailable_proc_tree_does_not_allow_mutation(self):
        with self.assertRaisesRegex(RuntimeError, "Cannot inspect live product"):
            product.live_product_output_processes(self.root / "absent")

    def test_refusal_reports_each_live_pid(self):
        self.process(501, self.debug / "chrome")
        self.process(502, self.release / "mb_browser_tests")
        with mock.patch.object(product, "live_product_output_processes",
                               return_value=self.live()):
            with self.assertRaisesRegex(RuntimeError, r"PID\(s\): 501, 502"):
                product.require_no_live_product_output_processes("build")

    def test_prepare_checks_before_any_identity_or_staging_work(self):
        with mock.patch.object(product, "require_no_live_product_output_processes",
                               side_effect=RuntimeError("live output")), \
                mock.patch.object(product, "check_identity") as check_identity:
            with self.assertRaisesRegex(RuntimeError, "live output"):
                product.prepare(SimpleNamespace(baseline_review=None))
        check_identity.assert_not_called()

    def test_gen_build_and_test_build_check_before_verify(self):
        for stage in ("gen", "build", "test-build"):
            with self.subTest(stage=stage), \
                    mock.patch.object(product, "require_no_live_product_output_processes",
                                      side_effect=RuntimeError("live output")), \
                    mock.patch.object(product, "verify") as verify:
                with self.assertRaisesRegex(RuntimeError, "live output"):
                    product.run_build_stage(SimpleNamespace(stage=stage,
                                                            profile="debug"))
                verify.assert_not_called()


if __name__ == "__main__":
    unittest.main()

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "test_environment_root_fallback.py"
SPEC = importlib.util.spec_from_file_location("fallback_runner", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class FakeBrowserTests:
    def __init__(self, receipt):
        self.receipt = receipt

    def validate_receipt(self, path):
        return self.receipt, Path("/owned/mb_browser_tests"), "binary-sha"


class FallbackRunnerTest(unittest.TestCase):
    def test_receipt_source_hash_is_required(self):
        source = MODULE_PATH.parents[1] / "test/environment_root_fallback_probe_browsertest.cc"
        source_hash = MODULE.file_sha(source)
        receipt = {"integration_sha256": "integration-sha",
                   "integration": {"files": {MODULE.SOURCE_RELATIVE: source_hash}}}
        with tempfile.TemporaryDirectory() as directory:
            receipt_path = Path(directory) / "receipt.json"
            receipt_path.write_text("{}")
            result = MODULE.validate_build_receipt(
                receipt_path, FakeBrowserTests(receipt))
        self.assertEqual(result[3], source_hash)
        receipt["integration"]["files"][MODULE.SOURCE_RELATIVE] = "wrong"
        with self.assertRaisesRegex(RuntimeError, "source hash"):
            MODULE.validate_build_receipt(receipt_path, FakeBrowserTests(receipt))

    def test_result_requires_exit_13_without_timeout_and_all_checks(self):
        checks = {"checkpoint": True, "default_absent": True}
        self.assertTrue(MODULE.result_passes(13, False, checks))
        self.assertFalse(MODULE.result_passes(0, False, checks))
        self.assertFalse(MODULE.result_passes(13, True, checks))
        checks["default_absent"] = False
        self.assertFalse(MODULE.result_passes(13, False, checks))

    def test_cleanup_does_not_signal_reused_exited_pid(self):
        process = mock.Mock()
        process.pid = 12345
        process.wait.return_value = 13
        with mock.patch.object(MODULE.os, "killpg") as killpg:
            with mock.patch.object(MODULE.os, "waitid",
                                   side_effect=ChildProcessError()):
                cleanup = MODULE.terminate_group(process)
        self.assertFalse(cleanup["owned"])
        killpg.assert_not_called()

    def test_cleanup_kills_group_after_unreaped_leader_exit(self):
        process = mock.Mock()
        process.pid = 12345
        info = mock.Mock(si_code=MODULE.os.CLD_EXITED, si_status=13)
        with mock.patch.object(MODULE.os, "waitid", return_value=info), \
             mock.patch.object(MODULE.os, "killpg") as killpg:
            cleanup = MODULE.terminate_group(process)
        self.assertTrue(cleanup["owned"])
        self.assertTrue(cleanup["group_terminated"])
        self.assertEqual(cleanup["exit_code"], 13)
        self.assertEqual(killpg.call_args_list, [
            mock.call(process.pid, MODULE.signal.SIGTERM),
            mock.call(process.pid, MODULE.signal.SIGKILL),
        ])
        process.wait.assert_called_once_with(timeout=5)

    def test_ownership_loss_during_cleanup_stops_signaling(self):
        process = mock.Mock(pid=12345)
        with mock.patch.object(MODULE.os, "waitid",
                               side_effect=[None, ChildProcessError()]), \
             mock.patch.object(MODULE.os, "killpg") as killpg:
            cleanup = MODULE.terminate_group(process)
        self.assertFalse(cleanup["owned"])
        killpg.assert_called_once_with(process.pid, MODULE.signal.SIGTERM)
        process.wait.assert_not_called()

    def test_term_timeout_escalates_before_reaping(self):
        process = mock.Mock(pid=12345)
        exited = mock.Mock(si_code=MODULE.os.CLD_KILLED, si_status=9)
        with mock.patch.object(MODULE, "peek_leader", return_value=None), \
             mock.patch.object(MODULE, "wait_for_leader",
                               side_effect=[TimeoutError(), exited]), \
             mock.patch.object(MODULE.os, "killpg") as killpg:
            cleanup = MODULE.terminate_group(process)
        self.assertTrue(cleanup["owned"])
        self.assertEqual(cleanup["exit_code"], -9)
        self.assertEqual(killpg.call_args_list[0],
                         mock.call(process.pid, MODULE.signal.SIGTERM))
        self.assertIn(mock.call(process.pid, MODULE.signal.SIGKILL),
                      killpg.call_args_list)
        process.wait.assert_called_once_with(timeout=5)

    def test_timeout_cleanup_uses_term_then_kill_while_owned(self):
        process = mock.Mock()
        process.pid = 12345
        running = None
        exited = mock.Mock(si_code=MODULE.os.CLD_EXITED, si_status=15)
        with mock.patch.object(MODULE.os, "waitid", side_effect=[running, exited]), \
             mock.patch.object(MODULE.os, "killpg") as killpg:
            cleanup = MODULE.terminate_group(process)
        self.assertTrue(cleanup["owned"])
        self.assertEqual(cleanup["exit_code"], 15)
        self.assertEqual(killpg.call_args_list, [
            mock.call(process.pid, MODULE.signal.SIGTERM),
            mock.call(process.pid, MODULE.signal.SIGKILL),
        ])


if __name__ == "__main__":
    unittest.main()

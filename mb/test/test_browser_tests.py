import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import browser_tests


class BrowserTestEvidenceTest(unittest.TestCase):
    def test_tmpdir_fits_native_socket_limit_and_is_private(self):
        with tempfile.TemporaryDirectory(prefix="bt-check-") as directory:
            root = Path(directory)
            (root / ".build/tmp").mkdir(parents=True)
            with mock.patch.object(browser_tests, "ROOT", root):
                paths = browser_tests.make_private_dirs("20260908T195234.345213Z")
            socket_path = paths["tmp"] / "org.chromium.Chromium.XXXXXX/SingletonSocket"
            self.assertLessEqual(len(os.fsencode(socket_path)), 107)
            self.assertEqual(paths["tmp"].stat().st_mode & 0o777, 0o700)
            self.assertNotEqual(paths["tmp"].parent, paths["base"])

    def check_summary(self, value):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "summary.json"
            path.write_text(json.dumps(value))
            return browser_tests.validate_summary(path)

    def test_nonempty_successful_results_are_accepted(self):
        value = {"per_iteration_data": [{
            "MbTabTest.Operations": [{"status": "SUCCESS"}],
            "MbTabTest.Pins": [{"status": "SUCCESS"}],
        }]}
        self.assertEqual(self.check_summary(value), value)

    def test_empty_filter_result_does_not_pass(self):
        for value in ({}, {"per_iteration_data": []},
                      {"per_iteration_data": [{}]},
                      {"per_iteration_data": [{"MbTest.Empty": []}]}):
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                self.check_summary(value)

    def test_failed_or_skipped_run_is_not_hidden_by_success(self):
        for status in ("FAILURE", "CRASH", "TIMEOUT", "SKIPPED"):
            with self.subTest(status=status), self.assertRaises(RuntimeError):
                self.check_summary({"per_iteration_data": [{
                    "MbTest.Retry": [{"status": status}, {"status": "SUCCESS"}],
                }]})

    def test_malformed_record_is_not_silently_ignored(self):
        with self.assertRaises(RuntimeError):
            self.check_summary({"per_iteration_data": [{
                "MbTest.Malformed": [{"status": "SUCCESS"}, None],
            }]})


if __name__ == "__main__":
    unittest.main()

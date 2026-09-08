import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from upstream_smoke import process_title, wm_class_values


class ProcessTitleTest(unittest.TestCase):
    def test_chromium_linux_title_and_exec_argv_have_same_identity(self):
        # SetProcessTitleFromCommandLine joins argv with spaces. Treating its
        # output as a conventional NUL-separated argv loses renderer flags.
        arguments = [b"/test/chrome", b"--type=renderer", b"--lang=en-US"]
        expected = "/test/chrome --type=renderer --lang=en-US"
        self.assertEqual(process_title(b"\0".join(arguments) + b"\0"), expected)
        self.assertEqual(process_title(b" ".join(arguments) + b"\0" * 80), expected)


class WmClassTest(unittest.TestCase):
    def test_xprop_wm_class_has_separate_name_and_class(self):
        self.assertEqual(
            wm_class_values('WM_CLASS(STRING) = "mb (/tmp/profile)", "com.example.mb"'),
            ("mb (/tmp/profile)", "com.example.mb"))

    def test_rejects_missing_wm_class_part(self):
        with self.assertRaisesRegex(RuntimeError, "WM_CLASS"):
            wm_class_values('WM_CLASS(STRING) = "com.example.mb"')


if __name__ == "__main__":
    unittest.main()

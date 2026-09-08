import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from upstream_smoke import process_title


class ProcessTitleTest(unittest.TestCase):
    def test_chromium_linux_title_and_exec_argv_have_same_identity(self):
        # SetProcessTitleFromCommandLine joins argv with spaces. Treating its
        # output as a conventional NUL-separated argv loses renderer flags.
        arguments = [b"/test/chrome", b"--type=renderer", b"--lang=en-US"]
        expected = "/test/chrome --type=renderer --lang=en-US"
        self.assertEqual(process_title(b"\0".join(arguments) + b"\0"), expected)
        self.assertEqual(process_title(b" ".join(arguments) + b"\0" * 80), expected)


if __name__ == "__main__":
    unittest.main()

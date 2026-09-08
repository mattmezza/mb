"""End-to-end checks of the compiled control command using disposable files."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[2]
IDENTITY = tomllib.loads((ROOT / "mb/branding.toml").read_text())["product"]
BINARY = ROOT / ".build/control" / (IDENTITY["executable_name"] + "ctl")


class ControlTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / ".build/control")
        self.directory = Path(self.temp.name)
        self.filename = self.directory / "config.toml"
        self.env = dict(os.environ, HOME=str(self.directory),
                        XDG_CONFIG_HOME=str(self.directory / "xdg"))
        self.write_config()

    def tearDown(self):
        self.temp.cleanup()

    def write_config(self, personal="environments/personal", work="environments/work", extra=""):
        self.filename.write_text(
            "schema_version = 1\n"
            f"[environments.personal]\ndata_directory = {json.dumps(personal)}\n"
            f"[environments.work]\ndata_directory = {json.dumps(work)}\n" + extra)

    def run_control(self, *args, override=True, env=None):
        command = [str(BINARY)]
        if override:
            command += ["--config", str(self.filename)]
        return subprocess.run(command + list(args), cwd=self.directory,
                              env=self.env if env is None else env, text=True,
                              capture_output=True, timeout=5)

    def test_validate_list_and_path_do_not_create_environment_data(self):
        before = list(self.directory.rglob("*"))
        result = self.run_control("config", "validate")
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_control("environment", "list")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "personal\nwork\n")
        result = self.run_control("environment", "path", "work")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), str(self.directory / "environments/work"))
        self.assertEqual(list(self.directory.rglob("*")), before)

    def test_xdg_default_is_derived_from_manifest(self):
        default = Path(self.env["XDG_CONFIG_HOME"]) / IDENTITY["profile_directory_name"] / "config.toml"
        default.parent.mkdir(parents=True)
        default.write_bytes(self.filename.read_bytes())
        result = self.run_control("config", "validate", override=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(str(default), result.stdout)

    def test_home_default_when_xdg_unset(self):
        default = self.directory / ".config" / IDENTITY["profile_directory_name"] / "config.toml"
        default.parent.mkdir(parents=True)
        default.write_bytes(self.filename.read_bytes())
        env = dict(self.env)
        env.pop("XDG_CONFIG_HOME")
        self.assertEqual(self.run_control("config", "validate", override=False, env=env).returncode, 0)

    def test_relative_config_and_home_environment_expansion(self):
        self.write_config(personal="~/data/personal")
        result = self.run_control("environment", "path", "personal", "--config=config.toml", override=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), str(self.directory / "data/personal"))

    def test_invalid_toml_and_unknown_keys_report_filename_and_key(self):
        self.write_config(extra="secret = 'forbidden'\n")
        result = self.run_control("config", "validate")
        self.assertEqual(result.returncode, 2)
        self.assertIn(str(self.filename), result.stderr)
        self.assertIn("environments.work.secret", result.stderr)
        self.assertFalse((self.directory / "environments").exists())

    def test_duplicate_selector_aliases_fail(self):
        result = self.run_control("config", "validate", "-config=config.toml")
        self.assertEqual(result.returncode, 2)
        self.assertIn("more than once", result.stderr)

    def test_nested_roots_fail_without_creation(self):
        self.write_config(work="environments/personal/work")
        result = self.run_control("config", "validate")
        self.assertEqual(result.returncode, 2)
        self.assertIn("data_directory", result.stderr)
        self.assertFalse((self.directory / "environments").exists())

    def test_environment_symlinks_are_rejected(self):
        real = self.directory / "real"
        real.mkdir(mode=0o700)
        (self.directory / "alias").symlink_to(real, target_is_directory=True)
        self.write_config(personal="alias/personal")
        result = self.run_control("config", "validate")
        self.assertEqual(result.returncode, 2)
        self.assertIn("symlinks forbidden", result.stderr)
        self.assertEqual(list(real.iterdir()), [])

    def test_unsafe_root_is_not_chmodded(self):
        root = self.directory / "environments/personal"
        root.mkdir(parents=True)
        root.chmod(0o755)
        result = self.run_control("config", "validate")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(root.stat().st_mode & 0o777, 0o755)

    def test_config_symlink_uses_canonical_config_parent(self):
        alias = self.directory / "alias/config.toml"
        alias.parent.mkdir()
        alias.symlink_to(self.filename)
        result = self.run_control("--config", str(alias), "environment", "path", "personal", override=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), str(self.directory / "environments/personal"))

    def test_missing_file_and_nonregular_files_fail_promptly(self):
        fifo = self.directory / "fifo"
        os.mkfifo(fifo)
        for source in (self.directory / "missing", self.directory, fifo):
            result = self.run_control("--config", str(source), "config", "validate", override=False)
            self.assertEqual(result.returncode, 2)
            self.assertIn("<file>", result.stderr)

    def test_oversize_file_fails_before_parsing(self):
        self.filename.write_text(" " * (1024 * 1024 + 1))
        result = self.run_control("config", "validate")
        self.assertEqual(result.returncode, 2)
        self.assertIn("1 MiB", result.stderr)

    def test_invalid_home_or_xdg_is_rejected(self):
        for key in ("HOME", "XDG_CONFIG_HOME"):
            env = dict(self.env, **{key: "relative"})
            result = self.run_control("config", "validate", override=False, env=env)
            self.assertEqual(result.returncode, 2)
            self.assertIn(key, result.stderr)

    def test_unknown_environment_command_and_browser_flags_fail(self):
        for args in (("environment", "path", "unknown"), ("unknown",),
                     ("config", "validate", "--incognito"),
                     ("config", "validate", "-user-data-dir=/tmp/unused")):
            self.assertEqual(self.run_control(*args).returncode, 2)

    def test_help_requires_no_configuration(self):
        self.filename.unlink()
        result = self.run_control("--help", override=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(IDENTITY["executable_name"] + "ctl", result.stdout)

    def test_diagnostics_escape_terminal_controls_and_invalid_utf8(self):
        for sequence in (b"\x1b[2J", b"\x9b2J", b"\xc2\x9b2J", b"\xff", b"\xe2\x80\xae"):
            filename = os.fsencode(self.directory) + b"/missing-" + sequence
            result = subprocess.run(
                [os.fsencode(BINARY), b"--config", filename, b"config", b"validate"],
                env=self.env, capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 2)
            self.assertNotIn(sequence, result.stderr)
            self.assertIn(b"\\x", result.stderr)

    def test_diagnostics_preserve_printable_unicode(self):
        result = self.run_control("--config", "missing-è-🌍.toml", "config", "validate", override=False)
        self.assertEqual(result.returncode, 2)
        self.assertIn("missing-è-🌍.toml", result.stderr)

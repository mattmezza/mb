import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import upstream  # noqa: E402


class VerifyDependenciesTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.checkout = Path(self.temporary_directory.name) / "chromium"
        (self.checkout / "src").mkdir(parents=True)

    def tearDown(self):
        self.temporary_directory.cleanup()

    @staticmethod
    def git(repo, *args):
        return subprocess.check_output(
            ["git", "-C", str(repo), *args], text=True
        ).strip()

    def make_repo(self, relative_path="src/third_party/example"):
        repo = self.checkout / relative_path
        repo.mkdir(parents=True, exist_ok=True)
        self.git(repo, "init", "--quiet")
        self.git(repo, "config", "user.name", "Test User")
        self.git(repo, "config", "user.email", "test@example.invalid")
        (repo / "tracked.txt").write_text("original\n")
        self.git(repo, "add", "tracked.txt")
        self.git(repo, "commit", "--quiet", "-m", "fixture")
        return repo, self.git(repo, "rev-parse", "HEAD")

    def write_entries(self, entries):
        (self.checkout / ".gclient_entries").write_text(
            "entries = " + repr(entries) + "\n"
        )

    @staticmethod
    def pinned_url(revision):
        return f"https://example.invalid/repository.git@{revision}"

    def test_accepts_clean_nested_repository_and_skips_non_git_entries(self):
        repo, revision = self.make_repo()
        self.write_entries(
            {
                "src": self.pinned_url("0" * 40),
                "src/third_party/example": self.pinned_url(revision),
                "src/third_party/package:tool": "infra/package/name@version:1",
                "src/optional": None,
            }
        )

        actual = upstream.verify_dependencies(self.checkout)

        self.assertEqual(actual, {"src/third_party/example": revision})
        self.assertTrue((repo / ".git").is_dir())

    def test_rejects_modified_tracked_file(self):
        repo, revision = self.make_repo()
        self.write_entries({"src/third_party/example": self.pinned_url(revision)})
        (repo / "tracked.txt").write_text("modified\n")

        with self.assertRaises(RuntimeError):
            upstream.verify_dependencies(self.checkout)

    def test_rejects_untracked_file(self):
        repo, revision = self.make_repo()
        self.write_entries({"src/third_party/example": self.pinned_url(revision)})
        (repo / "untracked.txt").write_text("untracked\n")

        with self.assertRaises(RuntimeError):
            upstream.verify_dependencies(self.checkout)

    def test_rejects_wrong_revision(self):
        _, revision = self.make_repo()
        wrong_revision = ("0" if revision[0] != "0" else "1") + revision[1:]
        self.write_entries(
            {"src/third_party/example": self.pinned_url(wrong_revision)}
        )

        with self.assertRaises(RuntimeError):
            upstream.verify_dependencies(self.checkout)

    def test_rejects_path_escaping_source_checkout(self):
        outside, revision = self.make_repo("outside")
        self.write_entries({"src/../outside": self.pinned_url(revision)})

        with self.assertRaises(RuntimeError):
            upstream.verify_dependencies(self.checkout)
        self.assertTrue(outside.is_dir())

    def test_rejects_directory_that_only_inherits_parent_git_repository(self):
        source, revision = self.make_repo("src")
        dependency = source / "third_party/inherited"
        dependency.mkdir(parents=True)
        (dependency / "owned-by-parent.txt").write_text("parent repository\n")
        self.git(source, "add", "third_party/inherited/owned-by-parent.txt")
        self.git(source, "commit", "--quiet", "-m", "add inherited directory")
        revision = self.git(source, "rev-parse", "HEAD")
        self.write_entries(
            {"src/third_party/inherited": self.pinned_url(revision)}
        )

        with self.assertRaises(RuntimeError):
            upstream.verify_dependencies(self.checkout)

    def test_rejects_malformed_entries_file(self):
        (self.checkout / ".gclient_entries").write_text(
            "entries = {}\nunexpected = {}\n"
        )

        with self.assertRaises(RuntimeError):
            upstream.verify_dependencies(self.checkout)

    def test_rejects_executable_entries_without_executing_it(self):
        marker = Path(self.temporary_directory.name) / "executed"
        expression = (
            "__import__('pathlib').Path("
            + repr(str(marker))
            + ").write_text('executed') or {}"
        )
        (self.checkout / ".gclient_entries").write_text(
            f"entries = {expression}\n"
        )

        with self.assertRaises(RuntimeError):
            upstream.verify_dependencies(self.checkout)
        self.assertFalse(marker.exists())


if __name__ == "__main__":
    unittest.main()

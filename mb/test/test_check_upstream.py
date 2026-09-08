import copy
from contextlib import redirect_stderr, redirect_stdout
import io
import sys
import tomllib
import unittest
from urllib.error import HTTPError
from urllib.request import Request
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import check_upstream  # noqa: E402


VERSION = "153.0.8000.42"
COMMIT = "a" * 40


def release(version=VERSION, channel="Stable", platform="Linux"):
    return [{"version": version, "channel": channel, "platform": platform,
             "hashes": {"chromium": COMMIT}}]


def installed_pin(version="152.0.7977.82", commit="d" * 40):
    return {
        "schema_version": 1,
        "chromium": {
            "version": version,
            "commit": commit,
            "tag": f"refs/tags/{version}",
            "branch": f"refs/branch-heads/{version.split('.')[2]}",
            "selected_date": "2026-09-08",
            "channel": "Stable",
            "platform": "Linux",
            "repository": check_upstream.CHROMIUM_REPOSITORY,
        },
        "depot_tools": {
            "commit": "b" * 40,
            "selected_date": "2026-09-08",
            "repository": "https://chromium.googlesource.com/chromium/tools/depot_tools.git",
        },
    }


class CandidateCheckTest(unittest.TestCase):
    def test_builds_candidate_from_only_fixed_official_urls(self):
        calls = []

        def get_json(url):
            calls.append(url)
            if url == check_upstream.RELEASE_SOURCE:
                return release()
            self.assertEqual(url, check_upstream.tag_url(VERSION))
            return {"commit": COMMIT}

        candidate = check_upstream.fetch_candidate(get_json, "2026-09-08")

        self.assertEqual(calls, [check_upstream.RELEASE_SOURCE, check_upstream.tag_url(VERSION)])
        self.assertEqual(candidate["commit"], COMMIT)
        self.assertEqual(candidate["tag"], "refs/tags/153.0.8000.42")
        self.assertEqual(candidate["branch"], "refs/branch-heads/8000")
        self.assertEqual(candidate["selected_date"], "2026-09-08")
        self.assertEqual(candidate["release_source"], check_upstream.RELEASE_SOURCE)
        self.assertEqual(candidate["verification_source"], check_upstream.tag_url(VERSION))

    def test_rejects_malformed_or_wrong_release_metadata(self):
        for value in ([], [{"version": "153.bad.8000.42", "channel": "Stable", "platform": "Linux"}],
                      release(channel="Beta"), release(platform="Mac")):
            with self.subTest(value=value):
                with self.assertRaises(check_upstream.CheckError):
                    check_upstream.fetch_candidate(lambda _: value, "2026-09-08")

    def test_rejects_non_exact_gitiles_commit(self):
        responses = iter((release(), {"commit": "A" * 40}))
        with self.assertRaises(check_upstream.CheckError):
            check_upstream.fetch_candidate(lambda _: next(responses), "2026-09-08")

    def test_rejects_disagreement_between_official_metadata_sources(self):
        responses = iter((release(), {"commit": "c" * 40}))
        with self.assertRaises(check_upstream.CheckError):
            check_upstream.fetch_candidate(lambda _: next(responses), "2026-09-08")

    def test_rejects_invalid_gitiles_endpoint_generation(self):
        for version in ("153.0.8000.42/escape", "153.0.8000", "153.0.08000.42"):
            with self.subTest(version=version):
                with self.assertRaises(check_upstream.CheckError):
                    check_upstream.tag_url(version)
        self.assertFalse(check_upstream.is_official_metadata_url(
            check_upstream.GITILES_PREFIX + "153.0.8000.42?format=JSON&extra=1"))
        self.assertFalse(check_upstream.is_official_metadata_url(
            "https://example.invalid/chromium/src/+/refs/tags/153.0.8000.42?format=JSON"))

    def test_rejects_oversized_version_components_without_value_error(self):
        enormous = "9" * 10000
        with self.assertRaises(check_upstream.CheckError):
            check_upstream.tag_url(f"{enormous}.0.8000.42")

    def test_fetch_json_rejects_redirects_before_following_them(self):
        handler = check_upstream.NoRedirectHandler()
        with self.assertRaises(HTTPError):
            handler.redirect_request(Request(check_upstream.RELEASE_SOURCE), None, 302,
                                     "Found", {}, "https://example.invalid/redirect")

        class RedirectingOpener:
            def __init__(self):
                self.requests = []

            def open(self, request, timeout):
                self.requests.append((request.full_url, timeout))
                raise HTTPError(request.full_url, 302, "Found", {}, None)

        opener = RedirectingOpener()
        with self.assertRaises(check_upstream.CheckError):
            check_upstream.fetch_json(check_upstream.RELEASE_SOURCE, opener)
        self.assertEqual(opener.requests, [(check_upstream.RELEASE_SOURCE, 20)])

    def test_fetch_json_rejects_bad_json_and_oversized_responses(self):
        class Response:
            def __init__(self, payload):
                self.payload = payload

            def __enter__(self):
                return self

            def __exit__(self, exc_type, exc_value, traceback):
                return False

            def geturl(self):
                return check_upstream.RELEASE_SOURCE

            def read(self, limit):
                if limit != 1024 * 1024 + 1:
                    raise AssertionError("unexpected read limit")
                return self.payload

        class Opener:
            def __init__(self, payload):
                self.payload = payload

            def open(self, request, timeout):
                return Response(self.payload)

        for payload in (b"not json", b"x" * (1024 * 1024 + 1),
                        b"[" * 2000 + b"]" * 2000, b"9" * 5000):
            with self.subTest(length=len(payload)):
                with self.assertRaises(check_upstream.CheckError):
                    check_upstream.fetch_json(check_upstream.RELEASE_SOURCE, Opener(payload))

    def test_compare_and_emitted_toml_distinguish_candidate_from_installed(self):
        installed, depot = check_upstream.validate_installed_pin(installed_pin())
        candidate = check_upstream.candidate_from_responses(
            release(), {"commit": COMMIT}, "2026-09-08")
        relation = check_upstream.compare(installed, candidate)
        output = check_upstream.emit_candidate(candidate, depot, relation)

        self.assertEqual(relation, "newer")
        self.assertIn('record_type = "upstream_candidate"', output)
        self.assertIn('comparison_to_installed = "newer"', output)
        self.assertIn('[chromium]', output)
        self.assertIn('[depot_tools]', output)
        self.assertIn('depot_tools_validation = "retained from the installed pin;', output)
        self.assertNotIn("announcement", output)
        parsed = tomllib.loads(output)
        self.assertEqual(parsed["chromium"]["commit"], COMMIT)
        self.assertEqual(parsed["depot_tools"]["commit"], depot["commit"])

        retained_with_escapes = dict(depot, repository='retained "quoted" \\ value')
        escaped = tomllib.loads(check_upstream.emit_candidate(
            candidate, retained_with_escapes, relation))
        self.assertEqual(escaped["depot_tools"]["repository"], retained_with_escapes["repository"])

    def test_rejects_invalid_installed_pin_instead_of_trusting_it(self):
        bad = installed_pin()
        bad["chromium"]["repository"] = "https://example.invalid/src.git"
        with self.assertRaises(check_upstream.CheckError):
            check_upstream.validate_installed_pin(bad)

    def test_rejects_malformed_installed_pin_fields(self):
        mutations = (
            lambda pin: pin.update(schema_version=True),
            lambda pin: pin["chromium"].update(tag="refs/tags/elsewhere"),
            lambda pin: pin["chromium"].update(branch="refs/branch-heads/1"),
            lambda pin: pin["chromium"].update(selected_date="2026-02-30"),
            lambda pin: pin["chromium"].update(channel="Beta"),
            lambda pin: pin["chromium"].update(platform="Mac"),
            lambda pin: pin["depot_tools"].update(
                repository="https://example.invalid/depot_tools.git"),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                pin = copy.deepcopy(installed_pin())
                mutate(pin)
                with self.assertRaises(check_upstream.CheckError):
                    check_upstream.validate_installed_pin(pin)

    def test_cli_supports_help_and_rejects_extra_options(self):
        help_output = io.StringIO()
        with redirect_stdout(help_output), self.assertRaises(SystemExit) as help_exit:
            check_upstream.main(["--help"])
        self.assertEqual(help_exit.exception.code, 0)
        self.assertIn("usage: check_upstream.py", help_output.getvalue())
        option_output = io.StringIO()
        with redirect_stderr(option_output), self.assertRaises(SystemExit) as option_exit:
            check_upstream.main(["--ignored"])
        self.assertEqual(option_exit.exception.code, 2)
        self.assertIn("unrecognized arguments", option_output.getvalue())


if __name__ == "__main__":
    unittest.main()

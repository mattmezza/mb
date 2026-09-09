#!/usr/bin/env python3
"""Print a read-only Chromium Stable/Linux candidate pin as TOML.

The emitted record is a candidate only. It never changes the installed pin,
checkout, gclient state, or any build output.
"""

from __future__ import annotations

import argparse
from datetime import date, datetime, timezone
import json
from pathlib import Path
import re
import sys
import tomllib
from typing import Any, Callable
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import HTTPRedirectHandler, Request, build_opener


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PIN = ROOT / "mb/upstream-version.toml"
CHROMIUM_REPOSITORY = "https://chromium.googlesource.com/chromium/src.git"
RELEASE_SOURCE = (
    "https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Linux&num=1"
)
GITILES_PREFIX = "https://chromium.googlesource.com/chromium/src/+/refs/tags/"
VERSION_RE = re.compile(
    r"^(?P<major>0|[1-9][0-9]*)\.(?P<minor>0|[1-9][0-9]*)\."
    r"(?P<branch>0|[1-9][0-9]*)\.(?P<patch>0|[1-9][0-9]*)$"
)
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
DATE_RE = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}$")
MAX_VERSION_COMPONENT = 2**32 - 1
MAX_JSON_DEPTH = 64
MAX_JSON_NUMBER_DIGITS = 1000
DEPOT_TOOLS_REPOSITORY = "https://chromium.googlesource.com/chromium/tools/depot_tools.git"


class CheckError(RuntimeError):
    pass


class NoRedirectHandler(HTTPRedirectHandler):
    """Turn every redirect into an error before a follow-up request is sent."""

    def redirect_request(self, request, fp, code, message, headers, newurl):
        raise HTTPError(request.full_url, code, "redirects are not permitted", headers, fp)


def require_string(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value or any(ord(char) < 0x20 for char in value):
        raise CheckError(f"Invalid {name} in response")
    return value


def parse_version(version: str) -> tuple[int, int, int, int]:
    match = VERSION_RE.fullmatch(version)
    if not match:
        raise CheckError("ChromiumDash returned an invalid Chromium version")
    components = []
    for name in ("major", "minor", "branch", "patch"):
        raw = match.group(name)
        if len(raw) > 10:
            raise CheckError("ChromiumDash returned an oversized Chromium version component")
        component = int(raw)
        if component > MAX_VERSION_COMPONENT:
            raise CheckError("ChromiumDash returned an oversized Chromium version component")
        components.append(component)
    return components[0], components[1], components[2], components[3]


def validate_date(value: Any, name: str) -> str:
    text = require_string(value, name)
    if not DATE_RE.fullmatch(text):
        raise CheckError(f"Invalid {name}; expected YYYY-MM-DD")
    try:
        parsed = date.fromisoformat(text)
    except ValueError as error:
        raise CheckError(f"Invalid {name}; expected a real calendar date") from error
    if parsed.isoformat() != text:
        raise CheckError(f"Invalid {name}; expected YYYY-MM-DD")
    return text


def tag_url(version: str) -> str:
    parse_version(version)
    return GITILES_PREFIX + quote(version, safe=".") + "?format=JSON"


def is_official_metadata_url(url: str) -> bool:
    if url == RELEASE_SOURCE:
        return True
    if not url.startswith(GITILES_PREFIX):
        return False
    suffix = url[len(GITILES_PREFIX):]
    if not suffix.endswith("?format=JSON"):
        return False
    version = suffix.removesuffix("?format=JSON")
    try:
        return tag_url(version) == url
    except CheckError:
        return False


def validate_json_limits(text: str) -> None:
    """Reject costly JSON nesting and numeric conversion before json.loads."""
    depth = 0
    in_string = False
    escaped = False
    index = 0
    while index < len(text):
        char = text[index]
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            index += 1
            continue
        if char == '"':
            in_string = True
        elif char in "[{":
            depth += 1
            if depth > MAX_JSON_DEPTH:
                raise ValueError("JSON metadata nesting exceeds the supported limit")
        elif char in "]}":
            depth -= 1
        elif char == "-" or char.isdigit():
            end = index + 1
            while end < len(text) and text[end] not in " \t\r\n,]}:":
                end += 1
            if sum(character.isdigit() for character in text[index:end]) > MAX_JSON_NUMBER_DIGITS:
                raise ValueError("JSON metadata number exceeds the supported limit")
            index = end - 1
        index += 1


def fetch_json(url: str, opener: Any | None = None) -> Any:
    if not is_official_metadata_url(url):
        raise CheckError("Refusing a non-official metadata endpoint")
    request = Request(url, headers={"Accept": "application/json"})
    client = opener or build_opener(NoRedirectHandler())
    try:
        with client.open(request, timeout=20) as response:
            if response.geturl() != url:
                raise CheckError("Refusing redirected metadata response")
            data = response.read(1024 * 1024 + 1)
    except (HTTPError, URLError) as error:
        raise CheckError(f"Cannot fetch official metadata: {error}") from error
    if len(data) > 1024 * 1024:
        raise CheckError("Metadata response exceeds 1 MiB")
    try:
        text = data.decode("utf-8")
        if text.startswith(")]}'\n"):
            text = text[5:]
        validate_json_limits(text)
        return json.loads(text)
    except (UnicodeDecodeError, ValueError, RecursionError) as error:
        raise CheckError("Metadata response is not valid UTF-8 JSON") from error


def candidate_from_responses(
    release_response: Any, tag_response: Any, selected_date: str
) -> dict[str, str]:
    validate_date(selected_date, "selected UTC date")
    if not isinstance(release_response, list) or len(release_response) != 1:
        raise CheckError("ChromiumDash response must contain exactly one release")
    release = release_response[0]
    if not isinstance(release, dict):
        raise CheckError("ChromiumDash release must be an object")
    if release.get("channel") != "Stable" or release.get("platform") != "Linux":
        raise CheckError("ChromiumDash response is not Linux Stable")
    version = require_string(release.get("version"), "release version")
    _, _, branch, _ = parse_version(version)
    hashes = release.get("hashes")
    if not isinstance(hashes, dict):
        raise CheckError("ChromiumDash response lacks release hashes")
    dashboard_commit = require_string(hashes.get("chromium"), "ChromiumDash Chromium commit")
    if not COMMIT_RE.fullmatch(dashboard_commit):
        raise CheckError("ChromiumDash response does not contain an exact Chromium commit")
    if not isinstance(tag_response, dict):
        raise CheckError("Gitiles tag response must be an object")
    commit = require_string(tag_response.get("commit"), "Gitiles tag commit")
    if not COMMIT_RE.fullmatch(commit):
        raise CheckError("Gitiles tag response does not contain an exact commit")
    if commit != dashboard_commit:
        raise CheckError("ChromiumDash and Gitiles resolve the release tag differently")
    return {
        "version": version,
        "commit": commit,
        "tag": f"refs/tags/{version}",
        "branch": f"refs/branch-heads/{branch}",
        "selected_date": selected_date,
        "channel": "Stable",
        "platform": "Linux",
        "repository": CHROMIUM_REPOSITORY,
        "release_source": RELEASE_SOURCE,
        "verification_source": tag_url(version),
    }


def fetch_candidate(
    get_json: Callable[[str], Any] = fetch_json,
    selected_date: str | None = None,
) -> dict[str, str]:
    date = selected_date or datetime.now(timezone.utc).date().isoformat()
    releases = get_json(RELEASE_SOURCE)
    if not isinstance(releases, list) or len(releases) != 1 or not isinstance(releases[0], dict):
        raise CheckError("ChromiumDash response must contain exactly one release")
    version = require_string(releases[0].get("version"), "release version")
    return candidate_from_responses(releases, get_json(tag_url(version)), date)


def validate_installed_pin(pin: dict[str, Any]) -> tuple[dict[str, str], dict[str, str]]:
    chromium = pin.get("chromium")
    depot_tools = pin.get("depot_tools")
    if not isinstance(chromium, dict) or not isinstance(depot_tools, dict):
        raise CheckError("Installed pin lacks Chromium or depot_tools tables")
    if type(pin.get("schema_version")) is not int or pin["schema_version"] != 1:
        raise CheckError("Installed pin has an unsupported schema_version")
    chromium_required = ("version", "commit", "tag", "branch", "selected_date", "channel",
                         "platform", "repository")
    depot_required = ("commit", "selected_date", "repository")
    installed_chromium = {key: require_string(chromium.get(key), f"installed chromium.{key}")
                          for key in chromium_required}
    retained_depot = {key: require_string(depot_tools.get(key), f"installed depot_tools.{key}")
                      for key in depot_required}
    _, _, branch, _ = parse_version(installed_chromium["version"])
    if installed_chromium["tag"] != f"refs/tags/{installed_chromium['version']}":
        raise CheckError("Installed Chromium pin tag does not match its version")
    if installed_chromium["branch"] != f"refs/branch-heads/{branch}":
        raise CheckError("Installed Chromium pin branch does not match its version")
    validate_date(installed_chromium["selected_date"], "installed chromium.selected_date")
    validate_date(retained_depot["selected_date"], "installed depot_tools.selected_date")
    if not COMMIT_RE.fullmatch(installed_chromium["commit"]):
        raise CheckError("Installed Chromium pin does not contain an exact commit")
    if not COMMIT_RE.fullmatch(retained_depot["commit"]):
        raise CheckError("Installed depot_tools pin does not contain an exact commit")
    if installed_chromium["repository"] != CHROMIUM_REPOSITORY:
        raise CheckError("Installed Chromium pin has an unexpected repository")
    if installed_chromium["channel"] != "Stable" or installed_chromium["platform"] != "Linux":
        raise CheckError("Installed Chromium pin is not Linux Stable")
    if retained_depot["repository"] != DEPOT_TOOLS_REPOSITORY:
        raise CheckError("Installed depot_tools pin has an unexpected repository")
    return installed_chromium, retained_depot


def compare(installed: dict[str, str], candidate: dict[str, str]) -> str:
    if all(installed[key] == candidate[key] for key in ("version", "commit", "tag", "branch")):
        return "same"
    installed_version = parse_version(installed["version"])
    candidate_version = parse_version(candidate["version"])
    if candidate_version > installed_version:
        return "newer"
    if candidate_version < installed_version:
        return "older"
    return "different-build"


def toml_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def emit_candidate(
    candidate: dict[str, str], retained_depot: dict[str, str], comparison: str
) -> str:
    lines = [
        "# Candidate only: this output does not modify the installed pin or any checkout.",
        "schema_version = 1",
        'record_type = "upstream_candidate"',
        f"comparison_to_installed = {toml_string(comparison)}",
        ('depot_tools_validation = '
         '"retained from the installed pin; not validated against candidate DEPS or toolchain"'),
        "",
        "[chromium]",
    ]
    for key in ("version", "commit", "tag", "branch", "selected_date", "channel", "platform",
                "repository", "release_source", "verification_source"):
        lines.append(f"{key} = {toml_string(candidate[key])}")
    lines.extend(["", "[depot_tools]"])
    for key in ("commit", "selected_date", "repository"):
        lines.append(f"{key} = {toml_string(retained_depot[key])}")
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="check_upstream.py", description=__doc__)
    parser.parse_args(argv)
    try:
        installed, retained_depot = validate_installed_pin(
            tomllib.loads(DEFAULT_PIN.read_text(encoding="utf-8"))
        )
        candidate = fetch_candidate()
        relation = compare(installed, candidate)
        print(emit_candidate(candidate, retained_depot, relation), end="")
        print("read-only candidate check; installed pin was not changed", file=sys.stderr)
        print("installed Chromium: " + installed["version"] + " " + installed["commit"],
              file=sys.stderr)
        print("candidate Chromium: " + candidate["version"] + " " + candidate["commit"],
              file=sys.stderr)
        print("comparison: " + relation, file=sys.stderr)
        return 0
    except (CheckError, OSError, tomllib.TOMLDecodeError, URLError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

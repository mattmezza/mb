#!/usr/bin/env python3
"""Run logged build stages for the pinned, unmodified upstream baseline."""

import argparse
import ast
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / ".build"
DEPOT = BUILD / "depot_tools"
SOURCE = BUILD / "chromium/src"
OUTPUT = SOURCE / "out/mb-debug"
ARGS = ROOT / "mb/tools/gn/upstream-debug.gn"


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()


def verify_dependencies(checkout):
    """Check nested Git source against gclient's resolved pinned dependency list.

    Artifact entries contain ':' in their name; gclient verifies their download
    hashes. This checks source Git repositories, not extracted binary content.
    Never execute the generated entries file as Python.
    """
    path = checkout / ".gclient_entries"
    try:
        tree = ast.parse(path.read_text(), filename=str(path))
        if len(tree.body) != 1 or not isinstance(tree.body[0], ast.Assign):
            raise ValueError("expected one entries assignment")
        assignment = tree.body[0]
        if len(assignment.targets) != 1 or not isinstance(assignment.targets[0], ast.Name) or assignment.targets[0].id != "entries":
            raise ValueError("expected entries assignment")
        entries = ast.literal_eval(assignment.value)
        if not isinstance(entries, dict):
            raise ValueError("entries must be a dictionary")
    except (ValueError, SyntaxError) as error:
        raise RuntimeError(f"Invalid {path}: {error}") from error

    def check(item):
        name, url = item
        if not isinstance(name, str) or not (url is None or isinstance(url, str)):
            raise RuntimeError(f"Invalid dependency entry in {path}")
        if url is None or name == "src" or ":" in name:
            return None
        repo = (checkout / name).resolve()
        source = (checkout / "src").resolve()
        if repo == source or not repo.is_relative_to(source):
            raise RuntimeError(f"Dependency path escapes source tree: {name}")
        if not (repo / ".git").exists():
            raise RuntimeError(f"Dependency has no own Git checkout: {name}")
        expected = url.rsplit("@", 1)[-1]
        if not re.fullmatch(r"[0-9a-f]{40}", expected):
            raise RuntimeError(f"Dependency has no exact commit pin: {name}")
        actual = git(repo, "rev-parse", "HEAD")
        if actual != expected:
            raise RuntimeError(f"Dependency revision differs: {name}; expected {expected}, got {actual}")
        if git(repo, "status", "--porcelain", "--untracked-files=normal"):
            raise RuntimeError(f"Dependency has local changes: {name}")
        return name, actual

    with ThreadPoolExecutor(max_workers=4) as pool:
        result = dict(item for item in pool.map(check, entries.items()) if item is not None)
    print(f"Verified {len(result)} nested Git dependency revisions and clean working trees", flush=True)
    return result


def verify():
    pins = tomllib.loads((ROOT / "mb/upstream-version.toml").read_text())
    for key, repo in (("chromium", SOURCE), ("depot_tools", DEPOT)):
        if git(repo, "rev-parse", "HEAD") != pins[key]["commit"]:
            raise RuntimeError(f"{key} checkout is not at the recorded pin")
        if git(repo, "status", "--porcelain", "--untracked-files=normal"):
            raise RuntimeError(f"{repo} has local changes; this tool requires an unmodified baseline")
    if not (BUILD / "chromium/.gclient_entries").is_file():
        raise RuntimeError("Complete bootstrap.py fetch before running upstream stages")
    if not (DEPOT / "python-bin/python3").is_file():
        raise RuntimeError("Pinned depot_tools Python bootstrap has not completed")
    dependencies = verify_dependencies(BUILD / "chromium")
    return pins, dependencies


def run_logged(command, env, stage, pins, dependencies):
    logs = BUILD / "logs"
    logs.mkdir(exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    path = logs / f"upstream-{stage}-{stamp}.log"
    print(f"[{SOURCE}] {shlex.join(map(str, command))}", flush=True)
    print(f"Log: {path}", flush=True)
    started = datetime.now(timezone.utc)
    with path.open("x") as stream:
        stream.write(f"cwd: {SOURCE}\ncommand: {shlex.join(map(str, command))}\n")
        stream.flush()
        result = subprocess.run(command, cwd=SOURCE, env=env,
                                stdout=stream, stderr=subprocess.STDOUT, check=False)
    receipt = {
        "stage": stage, "started": started.isoformat(),
        "finished": datetime.now(timezone.utc).isoformat(),
        "exit_code": result.returncode, "log": str(path.relative_to(ROOT)),
        "command": list(map(str, command)), "chromium_commit": pins["chromium"]["commit"],
        "depot_tools_commit": pins["depot_tools"]["commit"],
        "git_dependencies": dependencies,
        "args_gn": (OUTPUT / "args.gn").read_text() if (OUTPUT / "args.gn").exists() else None,
    }
    if stage == "build" and result.returncode == 0:
        with (OUTPUT / "chrome").open("rb") as binary:
            receipt["binary_sha256"] = hashlib.file_digest(binary, "sha256").hexdigest()
    path.with_suffix(".json").write_text(json.dumps(receipt, indent=2) + "\n")
    if result.returncode:
        with path.open() as stream:
            from collections import deque
            print("".join(deque(stream, maxlen=50)), file=sys.stderr)
        raise RuntimeError(f"{stage} failed with exit {result.returncode}; see {path}")
    print(f"{stage} succeeded; receipt: {path.with_suffix('.json')}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", choices=("hooks", "gen", "build"))
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        pins, dependencies = verify()
        free = shutil.disk_usage(BUILD).free / 2**30
        print(f"Free working space: {free:.1f} GiB", flush=True)
        if free < 25:
            raise RuntimeError("Less than 25 GiB free reserve; inspect capacity before continuing")
        scratch = BUILD / "tmp"
        scratch.mkdir(exist_ok=True)
        env = dict(os.environ, PATH=f"{DEPOT}:{DEPOT / 'python-bin'}:{os.environ.get('PATH', '')}",
                   DEPOT_TOOLS_UPDATE="0", DEPOT_TOOLS_METRICS="0",
                   PYTHONUNBUFFERED="1", TMPDIR=str(scratch))
        if args.stage == "hooks":
            command = [str(DEPOT / "gclient"), "runhooks", "--jobs=4"]
        else:
            expected = ARGS.read_text()
            if args.stage == "gen":
                OUTPUT.mkdir(parents=True, exist_ok=True)
                path = OUTPUT / "args.gn"
                if path.exists() and path.read_text() != expected:
                    raise RuntimeError(f"Refusing to replace different build arguments: {path}")
                if not path.exists():
                    path.write_text(expected)
                command = [str(DEPOT / "gn"), "gen", "out/mb-debug", "--fail-on-unused-args"]
            else:
                if not (OUTPUT / "build.ninja").is_file():
                    raise RuntimeError("Run upstream.py gen successfully before building")
                if (OUTPUT / "args.gn").read_text() != expected:
                    raise RuntimeError("Output arguments differ from checked-in baseline arguments")
                command = [str(DEPOT / "autoninja"), "-C", "out/mb-debug", "-j", str(args.jobs), "chrome"]
        run_logged(command, env, args.stage, pins, dependencies)
        return 0
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

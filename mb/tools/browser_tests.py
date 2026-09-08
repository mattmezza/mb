#!/usr/bin/env python3
"""Run focused product browser tests from a shared desktop with isolated test storage."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[2]
import product


TARGET = "mb:mb_browser_tests"
MAX_TIMEOUT_SECONDS = 3600


def file_sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def receipt_target(receipt):
    return TARGET in receipt.get("command", [])


def binary_record(receipt):
    record = receipt.get("test_binaries", {}).get("mb_browser_tests")
    if not isinstance(record, dict):
        raise RuntimeError("test-build receipt has no mb_browser_tests test_binaries record")
    path = record.get("path")
    digest = record.get("sha256")
    if not isinstance(path, str) or not isinstance(digest, str):
        raise RuntimeError("mb_browser_tests test_binaries record lacks path and sha256")
    return Path(path), digest


def validate_receipt(path):
    receipt = json.loads(path.read_text())
    if receipt.get("stage") != "test-build" or receipt.get("exit_code") != 0:
        raise RuntimeError("A successful mb_browser_tests test-build receipt is required")
    if not receipt_target(receipt):
        raise RuntimeError("test-build receipt does not identify mb:mb_browser_tests")
    verified = product.verify()
    integration_sha = receipt.get("integration_sha256")
    if integration_sha != file_sha(product.RECEIPT):
        raise RuntimeError("test-build receipt integration hash differs from product receipt")
    if receipt.get("integration") != verified:
        raise RuntimeError("test-build receipt integration does not match product.verify()")
    recorded_binary, expected_sha = binary_record(receipt)
    binary = product.OUTPUT / "mb_browser_tests"
    if recorded_binary.resolve() != binary.resolve():
        raise RuntimeError("test-build receipt binary path is not the product mb_browser_tests output")
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise RuntimeError(f"Test binary is missing or not executable: {binary}")
    args_path = product.OUTPUT / "args.gn"
    expected_args = (ROOT / "mb/tools/gn/product-debug.gn").read_text()
    if receipt.get("args_gn") != expected_args or not args_path.is_file() or args_path.read_text() != expected_args:
        raise RuntimeError("Product output GN arguments differ from product-debug.gn")
    if file_sha(binary) != expected_sha:
        raise RuntimeError("mb_browser_tests executable hash differs from test-build receipt")
    return receipt, binary, expected_sha


def make_private_dirs(stamp):
    base = ROOT / ".build" / "test-data" / f"product-browser-tests-{stamp}"
    evidence = ROOT / ".build" / "test-evidence" / f"product-browser-tests-{stamp}"
    for path in (base, evidence):
        path.mkdir(mode=0o700, parents=True, exist_ok=False)
    paths = {
        "base": base,
        "evidence": evidence,
        "tmp": Path(tempfile.mkdtemp(prefix="bt-", dir=ROOT / ".build/tmp")),
        "xdg_config": base / "xdg-config",
        "xdg_data": base / "xdg-data",
        "xdg_cache": base / "xdg-cache",
    }
    # Chromium adds a random directory and SingletonSocket below TMPDIR.
    # Linux sockaddr_un.sun_path permits 107 pathname bytes plus the terminator.
    socket_example = paths["tmp"] / "org.chromium.Chromium.XXXXXX/SingletonSocket"
    if len(os.fsencode(socket_example)) > 107:
        raise RuntimeError("Checkout path is too long for Chromium test singleton sockets")
    for path in paths.values():
        if path in (base, evidence, paths["tmp"]):
            continue
        path.mkdir(mode=0o700)
    return paths


def terminate_group(process):
    if process.poll() is not None:
        return
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=10)


def validate_summary(path):
    if not path.is_file() or path.stat().st_size == 0:
        raise RuntimeError("Browser test summary is missing or empty")
    summary = json.loads(path.read_text())
    iterations = summary.get("per_iteration_data")
    if not isinstance(iterations, list) or not iterations:
        raise RuntimeError("Browser test summary has no per_iteration_data")
    count = 0
    for iteration in iterations:
        if not isinstance(iteration, dict) or not iteration:
            raise RuntimeError("Browser test summary contains an empty or invalid iteration")
        for runs in iteration.values():
            if not isinstance(runs, list) or not runs:
                raise RuntimeError("Browser test summary contains missing test runs")
            for record in runs:
                if not isinstance(record, dict) or record.get("status") != "SUCCESS":
                    raise RuntimeError("Browser test summary contains an unsuccessful test")
                count += 1
    if count == 0:
        raise RuntimeError("Browser test summary contains no test results")
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-receipt", type=Path, required=True)
    parser.add_argument("--filter", default="Mb*")
    parser.add_argument("--timeout", type=int, default=900,
                        help="test timeout in seconds (1-3600; default: 900)")
    args = parser.parse_args()
    if not 1 <= args.timeout <= MAX_TIMEOUT_SECONDS:
        parser.error(f"--timeout must be between 1 and {MAX_TIMEOUT_SECONDS} seconds")
    if os.geteuid() == 0 or not os.environ.get("DISPLAY"):
        raise RuntimeError("Run as a non-root desktop user with an accessible X11 DISPLAY")

    receipt, binary, expected_sha = validate_receipt(args.build_receipt)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    paths = make_private_dirs(stamp)
    summary = paths["evidence"] / "summary.json"
    log_path = paths["evidence"] / "browser-tests.log"
    argv = [str(binary), "--ozone-platform=x11",
            "--test-launcher-jobs=1", f"--test-launcher-summary-output={summary}",
            "--test-launcher-retry-limit=0", f"--gtest_filter={args.filter}"]
    ignored_extra_flags = sorted(name for name in os.environ if name.startswith("CHROME_EXTRA_FLAGS"))
    env = dict(os.environ, TMPDIR=str(paths["tmp"]), XDG_CONFIG_HOME=str(paths["xdg_config"]),
               XDG_DATA_HOME=str(paths["xdg_data"]), XDG_CACHE_HOME=str(paths["xdg_cache"]))
    for name in ignored_extra_flags:
        env.pop(name, None)
    result = {
        "build_receipt": str(args.build_receipt.resolve()),
        "binary": str(binary.resolve()),
        "binary_sha256": expected_sha,
        "display": env["DISPLAY"],
        "argv": argv,
        "environment_paths": {key: str(value) for key, value in paths.items()},
        "filter": args.filter,
        "timeout_seconds": args.timeout,
        "ignored_environment_names": ignored_extra_flags,
    }
    process = None
    started = time.monotonic()
    print(f"Evidence: {paths['evidence']}", flush=True)
    try:
        with log_path.open("x") as log:
            process = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT,
                                       env=env, start_new_session=True)
            result["pid"] = process.pid
            try:
                process.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                result["timed_out"] = True
                result["forced_cleanup"] = True
                terminate_group(process)
                raise RuntimeError(f"Browser test timed out after {args.timeout} seconds")
        result["exit_code"] = process.returncode
        if process.returncode != 0:
            raise RuntimeError(f"Browser test exited with {process.returncode}")
        validate_summary(summary)
        result["result"] = "passed"
        print(f"Focused browser tests passed. Evidence: {paths['evidence']}", flush=True)
        return 0
    except (RuntimeError, OSError, ValueError, json.JSONDecodeError, subprocess.SubprocessError) as error:
        result["result"] = "failed"
        result["error"] = str(error)
        result["exit_code"] = process.returncode if process is not None else None
        print(f"error: {error}", file=sys.stderr)
        return 1
    finally:
        if process is not None and process.poll() is None:
            result["forced_cleanup"] = True
            terminate_group(process)
        result["elapsed_seconds"] = round(time.monotonic() - started, 3)
        result["summary"] = str(summary)
        result["log"] = str(log_path)
        (paths["evidence"] / "results.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, ValueError, json.JSONDecodeError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)

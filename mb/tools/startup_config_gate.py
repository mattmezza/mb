#!/usr/bin/env python3
"""Run bounded expected-error checks against the staged product executable.

This future-work harness launches only the receipt-bound executable, with a
fresh private scratch tree per case. It tests failures that must occur before
selected-root creation. It never supplies CHROME_EXTRA_FLAGS, --no-sandbox,
remote-debugging endpoints, arbitrary URLs, or user-provided configuration
contents, except for the fixed invalid-FD --remote-debugging-pipe negative
case.
"""

from __future__ import annotations

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


MAX_TIMEOUT = 60
EXPECTED_ERROR = "expected_error"
UNSUPPORTED_PARAM_EXIT = 13  # chrome result enum: content last code (5) + 8


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def validate_exit_code(actual: int, expected: int) -> None:
    if actual != expected:
        raise RuntimeError(f"expected exit {expected}, got {actual}")


def assert_roots_absent(roots) -> None:
    created = [str(root) for root in roots if Path(root).exists()]
    if created:
        raise RuntimeError(f"environment roots were created before failure: {created}")


def validate_receipt(receipt_path: Path, root: Path, verifier=None, product_api=None) -> tuple[dict, Path]:
    receipt = json.loads(receipt_path.read_text())
    if receipt.get("stage") != "build" or receipt.get("exit_code") != 0:
        raise RuntimeError("a successful product build receipt is required")
    digest = receipt.get("binary_sha256")
    if not isinstance(digest, str):
        raise RuntimeError("build receipt lacks binary identity")
    if product_api is None:
        sys.path.insert(0, str(root / "mb/tools"))
        import product
        product_api = product
    if verifier is None:
        verified = product_api.verify()
    else:
        verified = verifier()
    if not isinstance(verified, dict) or not isinstance(verified.get("product"), dict):
        raise RuntimeError("product.verify() returned no product manifest")
    executable_name = verified["product"].get("executable_name")
    if not isinstance(executable_name, str) or not executable_name:
        raise RuntimeError("product manifest lacks executable_name")
    integration_receipt = root / ".build/product-integration.json"
    if receipt.get("integration") != verified:
        raise RuntimeError("build receipt integration differs from product.verify()")
    if receipt.get("integration_sha256") != sha256(integration_receipt):
        raise RuntimeError("build receipt integration hash differs from product receipt")
    profile, binary = product_api.expected_product_binary(receipt, executable_name)
    binary = Path(binary).resolve()
    output_args = root / ".build/chromium/src" / profile["output_directory"] / "args.gn"
    if output_args.read_text() != receipt.get("args_gn"):
        raise RuntimeError("built output args.gn differs from the build receipt")
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise RuntimeError(f"receipt binary is missing or not executable: {binary}")
    if sha256(binary) != digest:
        raise RuntimeError("product executable differs from the build receipt")
    return receipt, binary


def config_text(selected_root: Path, *, malformed_url: bool = False) -> str:
    other_url = '"https://[invalid-host]/"' if malformed_url else "[]"
    if malformed_url:
        work_urls = f"startup_urls = [{other_url}]"
    else:
        work_urls = "startup_urls = []"
    return f'''schema_version = 1

[app]
default_environment = "personal"
restore_last_environment = false

[environments.personal]
data_directory = "{selected_root.as_posix()}"
startup_urls = []

[environments.work]
data_directory = "{(selected_root.parent / "work").as_posix()}"
{work_urls}
'''


def cases(case_root: Path) -> list[dict]:
    specs = [
        ("invalid_toml", "malformed", "invalid TOML", []),
        ("unknown_environment", "valid", "not configured", ["--environment", "missing"]),
        ("malformed_unselected_url", "bad_url", "startup URL", ["--environment", "personal"]),
        ("mismatched_user_data_dir", "valid", "selected root", ["--environment", "personal", "--user-data-dir=WRONG"]),
        ("rejected_user_data_environment", "valid", "CHROME_USER_DATA_DIR", ["--environment", "personal"]),
        ("duplicate_environment_selector", "valid", "environment", ["--environment", "personal", "--environment", "work"]),
        ("missing_environment_selector", "valid", "environment", ["--environment="]),
        ("remote_pipe_missing_fds", "valid", "pipe", ["--remote-debugging-pipe", "--environment", "personal"]),
    ]
    items = []
    for name, kind, needle, suffix in specs:
        directory = case_root / name
        directory.mkdir(mode=0o700)
        selected_name = "personal-url" if kind == "bad_url" else "personal"
        selected = directory / selected_name
        config = directory / "config.toml"
        if kind == "malformed":
            config.write_text("schema_version = 1\n[environments.personal\n")
        else:
            config.write_text(config_text(selected, malformed_url=kind == "bad_url"))
        args = ["--config", str(config)]
        for value in suffix:
            args.append("--user-data-dir=" + str(directory / "wrong") if value == "--user-data-dir=WRONG" else value)
        roots = [selected, directory / "work"]
        if name == "mismatched_user_data_dir":
            roots.append(directory / "wrong")
        if name == "rejected_user_data_environment":
            roots.append(directory / "override")
        items.append({"name": name, "config": config, "args": args, "needle": needle,
                      "expected_roots": roots, "expected_exit_code": UNSUPPORTED_PARAM_EXIT,
                      "env": {"CHROME_USER_DATA_DIR": str(directory / "override")} if name == "rejected_user_data_environment" else {}})
    return items


class LeaderOwnershipLost(RuntimeError):
    pass


def peek_leader(process: subprocess.Popen):
    try:
        return os.waitid(os.P_PID, process.pid,
                         os.WEXITED | os.WNOHANG | os.WNOWAIT)
    except ChildProcessError as error:
        raise LeaderOwnershipLost(
            "owned process was reaped before group cleanup") from error


def wait_for_leader(process: subprocess.Popen, timeout: float):
    deadline = time.monotonic() + timeout
    while True:
        info = peek_leader(process)
        if info is not None:
            return info
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("owned process did not exit before timeout")
        time.sleep(min(0.05, remaining))


def terminate_group(process: subprocess.Popen) -> dict[str, object]:
    """Terminate only a process group whose leader remains our unreaped child."""
    try:
        info = peek_leader(process)
    except LeaderOwnershipLost:
        return {"owned": False, "group_terminated": False,
                "leader_reaped": False}
    group_terminated = False
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    else:
        group_terminated = True
    if info is None:
        try:
            info = wait_for_leader(process, 5)
        except LeaderOwnershipLost:
            return {"owned": False, "group_terminated": group_terminated,
                    "leader_reaped": False}
        except TimeoutError:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            else:
                group_terminated = True
            try:
                info = wait_for_leader(process, 5)
            except (TimeoutError, LeaderOwnershipLost):
                return {"owned": False, "group_terminated": group_terminated,
                        "leader_reaped": False}
    # The leader is still unreaped, so its process-group ID cannot have been
    # reused before this final descendant cleanup.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    else:
        group_terminated = True
    try:
        process.wait(timeout=5)
    except (subprocess.TimeoutExpired, ChildProcessError):
        return {"owned": False, "group_terminated": group_terminated,
                "leader_reaped": False}
    return {"owned": True, "group_terminated": group_terminated,
            "leader_reaped": True}


def run_case(binary: Path, item: dict, case_root: Path, timeout: int) -> dict:
    env = dict(os.environ)
    ignored = sorted(name for name in env if name.startswith("CHROME_EXTRA_FLAGS"))
    for name in ignored:
        env.pop(name, None)
    env.pop("CHROME_USER_DATA_DIR", None)
    for name in ("HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "TMPDIR"):
        private = case_root / name.lower()
        private.mkdir(mode=0o700, exist_ok=True)
        env[name] = str(private)
    for name, value in item.get("env", {}).items():
        env[name] = value
    argv = [str(binary), "--no-first-run", "--no-default-browser-check", *item["args"]]
    log = case_root / "process.log"
    result = {"name": item["name"], "argv": argv, "ignored_environment_names": ignored, "result": "failed"}
    process = None
    try:
        with log.open("x") as stream:
            process = subprocess.Popen(argv, cwd=case_root, env=env, stdout=stream,
                                       stderr=subprocess.STDOUT, start_new_session=True,
                                       close_fds=True)
            result["pid"] = process.pid
            try:
                result["exit_code"] = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                result["timed_out"] = True
                result["forced_cleanup"] = terminate_group(process)
                raise RuntimeError("expected-error process timed out")
        output = log.read_text(errors="replace")
        result["output_contains_expected_reason"] = item["needle"].lower() in output.lower()
        validate_exit_code(result["exit_code"], item["expected_exit_code"])
        if not result["output_contains_expected_reason"]:
            raise RuntimeError("exit was nonzero but expected diagnostic was absent")
        assert_roots_absent(item["expected_roots"])
        result["result"] = EXPECTED_ERROR
    except (OSError, RuntimeError) as error:
        result["error"] = str(error)
    finally:
        if process is not None and process.returncode is None:
            result["forced_cleanup"] = terminate_group(process)
        result["log"] = str(log)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-receipt", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=15)
    parser.add_argument("--evidence", type=Path)
    args = parser.parse_args()
    if not 1 <= args.timeout <= MAX_TIMEOUT:
        parser.error(f"--timeout must be between 1 and {MAX_TIMEOUT}")
    if os.geteuid() == 0:
        raise RuntimeError("run as the non-root desktop user")
    root = Path(__file__).resolve().parents[2]
    _, binary = validate_receipt(args.build_receipt.resolve(), root)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    evidence = (args.evidence or root / ".build" / "test-evidence" / f"browser-config-gate-{stamp}").resolve()
    evidence.mkdir(mode=0o700, parents=True, exist_ok=False)
    scratch = Path(tempfile.mkdtemp(prefix="case-", dir=evidence))
    scratch.chmod(0o700)
    results = []
    print(f"Evidence: {evidence}", flush=True)
    for item in cases(scratch):
        case_root = scratch / item["name"]
        results.append(run_case(binary, item, case_root, args.timeout))
    payload = {"binary": str(binary), "build_receipt": str(args.build_receipt.resolve()), "cases": results,
               "result": "passed" if all(r["result"] == EXPECTED_ERROR for r in results) else "failed"}
    (evidence / "results.json").write_text(json.dumps(payload, indent=2) + "\n")
    return 0 if payload["result"] == "passed" else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)

#!/usr/bin/env python3
"""Run only the disabled native fallback probe, retaining isolated evidence."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import signal
import stat
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
PRODUCT_TOOLS = ROOT / "mb" / "tools"
SOURCE_RELATIVE = "mb/test/environment_root_fallback_probe_browsertest.cc"
FILTER = "MbEnvironmentRootFallbackProbe.DISABLED_ExitBeforeNativeDefaultFallback"
MARKER = b"mb-environment-root-fallback-probe-v1\n"
REPLACEMENT = b"owned fallback probe obstacle\n"
REFUSAL = (b"configuration: selected environment directory is unavailable; "
           b"refusing native default-directory fallback\n")


def is_absent(path: Path) -> bool:
    # A broken symlink is an artifact, not absence.
    return not os.path.lexists(path)


def file_sha(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def load_browser_tests(path: Path):
    if path.name != "browser_tests.py" or not path.is_file():
        raise RuntimeError("--browser-tests-script must name an existing browser_tests.py")
    sys.path.insert(0, str(PRODUCT_TOOLS))
    import product  # noqa: PLC0415
    spec = importlib.util.spec_from_file_location("fixture_browser_tests", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load browser_tests.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    # Use checked-out product tooling, not a sibling scratch tree inferred by
    # browser_tests.py from its own location.
    module.product = product
    return module


def validate_build_receipt(path: Path, browser_tests_module):
    receipt, binary, expected_sha = browser_tests_module.validate_receipt(path)
    integration = receipt.get("integration")
    if not isinstance(integration, dict):
        raise RuntimeError("test-build receipt lacks integration metadata")
    recorded_source_sha = integration.get("files", {}).get(SOURCE_RELATIVE)
    source = Path(__file__).resolve().parents[1] / "test" / Path(SOURCE_RELATIVE).name
    if not source.is_file() or recorded_source_sha != file_sha(source):
        raise RuntimeError("receipt source hash does not match the fallback probe source")
    return receipt, binary, expected_sha, file_sha(source)


def result_passes(code: int, timed_out: bool, checks: dict[str, bool]) -> bool:
    return code == 13 and not timed_out and all(checks.values())


class LeaderOwnershipLost(RuntimeError):
    pass


def peek_leader(process: subprocess.Popen):
    try:
        return os.waitid(os.P_PID, process.pid,
                         os.WEXITED | os.WNOHANG | os.WNOWAIT)
    except ChildProcessError as error:
        raise LeaderOwnershipLost("owned process was reaped before group cleanup") from error


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


def wait_status(info) -> int:
    if info.si_code == getattr(os, "CLD_EXITED", 1):
        return info.si_status
    return -info.si_status


def terminate_group(process: subprocess.Popen) -> dict[str, object]:
    """Terminate the owned group while the leader PID remains unreaped."""
    try:
        info = peek_leader(process)
    except LeaderOwnershipLost:
        return {"owned": False, "group_terminated": False, "leader_reaped": False}
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
    # The leader is still an unreaped child, so its process-group ID cannot be
    # reused. Kill any descendant that survived TERM before reaping the leader.
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
            "leader_reaped": True, "exit_code": wait_status(info)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-receipt", type=Path, required=True)
    parser.add_argument("--browser-tests-script", type=Path, required=True)
    parser.add_argument("--evidence-parent", type=Path, required=True)
    parser.add_argument("--timeout-seconds", type=int, default=90)
    args = parser.parse_args()
    if not 1 <= args.timeout_seconds <= 600:
        parser.error("--timeout-seconds must be between 1 and 600")
    parent = args.evidence_parent.resolve(strict=True)
    if not parent.is_dir():
        parser.error("--evidence-parent must be an existing directory")
    browser_tests_module = load_browser_tests(args.browser_tests_script.resolve())
    receipt, binary, expected_sha, source_sha = validate_build_receipt(
        args.build_receipt.resolve(), browser_tests_module)

    # New directory only. Never overwrite, recursively delete or reuse a fixture.
    root = Path(tempfile.mkdtemp(prefix="environment-fallback-", dir=parent))
    root.chmod(0o700)
    for name in ("home", "xdg", "xdg-data", "xdg-state", "cache", "tmp"):
        (root / name).mkdir(mode=0o700)
    (root / "probe-marker").write_bytes(MARKER)
    (root / "config.toml").write_text(
        "schema_version = 1\n[environments.personal]\n"
        "data_directory = 'selected'\n", encoding="utf-8")

    env = os.environ.copy()
    for key in tuple(env):
        if key.startswith("CHROME_EXTRA_FLAGS") or key in {
            "CHROME_USER_DATA_DIR", "CHROME_CONFIG_HOME", "MB_TEST_FALLBACK_PROBE_ROOT"
        }:
            env.pop(key)
    env.update({
        "HOME": str(root / "home"),
        "XDG_CONFIG_HOME": str(root / "xdg"),
        "XDG_DATA_HOME": str(root / "xdg-data"),
        "XDG_STATE_HOME": str(root / "xdg-state"),
        "XDG_CACHE_HOME": str(root / "cache"),
        "TMPDIR": str(root / "tmp"),
        "CHROME_LOG_FILE": str(root / "chrome.log"),
        "MB_TEST_FALLBACK_PROBE_ROOT": str(root),
    })
    command = [str(binary), "--single-process-tests",
               "--gtest_also_run_disabled_tests", "--gtest_filter=" + FILTER,
               "--config=" + str(root / "config.toml"),
               "--environment=personal", "--user-data-dir=" + str(root / "selected")]
    record = {
        "schema": 1, "filter": FILTER, "binary": str(binary),
        "binary_sha256": expected_sha,
        "integration_sha256": receipt["integration_sha256"],
        "probe_source_sha256": source_sha,
        "command": command, "timeout_seconds": args.timeout_seconds,
        "status": "incomplete", "timed_out": False,
    }
    (root / "invocation.json").write_text(json.dumps(record, indent=2) + "\n")
    start = time.monotonic()
    with (root / "probe.log").open("wb") as log:
        process = subprocess.Popen(command, cwd=root, env=env, stdout=log,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        record["owned_pid"] = process.pid
        code = None
        try:
            info = wait_for_leader(process, args.timeout_seconds)
            code = wait_status(info)
        except TimeoutError:
            record["timed_out"] = True
        except LeaderOwnershipLost as error:
            record["ownership_lost"] = str(error)
        finally:
            cleanup = terminate_group(process)
            record["cleanup"] = cleanup
            if "exit_code" in cleanup:
                code = cleanup["exit_code"]
    record["exit_code"] = code
    record["elapsed_seconds"] = time.monotonic() - start
    checks = {
        "owned_cleanup_complete": bool(cleanup.get("owned") and cleanup.get("leader_reaped")),
        "exit_13": code == 13 and not record["timed_out"],
        "after_real_gate_checkpoint": (root / "after-gate-checkpoint").is_file()
            and (root / "after-gate-checkpoint").read_bytes() == MARKER,
        "fixed_refusal_diagnostic": REFUSAL in (root / "probe.log").read_bytes(),
        "backup_directory_retained": (root / "prepared-backup").is_dir()
            and not (root / "prepared-backup").is_symlink(),
    }
    selected = root / "selected"
    checks["selected_obstacle_retained"] = (
        os.path.lexists(selected) and stat.S_ISREG(selected.lstat().st_mode)
        and selected.read_bytes() == REPLACEMENT)
    default_path_file = root / "native-default-path"
    default_root = None
    if default_path_file.is_file():
        default_root = Path(default_path_file.read_text(encoding="utf-8"))
    # Do not inspect any default path outside the private tree, even on failure.
    allowed_default = (default_root is not None and default_root.is_absolute()
                       and default_root.parent == root / "xdg"
                       and ".." not in default_root.parts)
    checks["native_default_inside_fixture"] = allowed_default
    checks["native_default_absent"] = bool(allowed_default and is_absent(default_root))
    # Default absence is stronger than individual missing Profile/Local State/
    # SingletonLock files; retain that exact assertion as the acceptance check.
    checks["default_parent_empty"] = not any((root / "xdg").iterdir())
    record["checks"] = checks
    record["status"] = "passed" if result_passes(code, record["timed_out"], checks) else "failed"
    (root / "result.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps({"status": record["status"], "evidence": str(root)}))
    return 0 if record["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())

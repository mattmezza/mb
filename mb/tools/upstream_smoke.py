#!/usr/bin/env python3
"""Exercise a successfully built upstream browser on X11 in a fresh test root.

Screenshots require review before declaring the upstream launch gate passed.
All input, capture and shutdown operations target the child browser we create.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import tomllib
from urllib.parse import quote

import upstream

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / ".build"
BINARY = BUILD / "chromium/src/out/mb-debug/chrome"


def command(*args, timeout=10):
    return subprocess.check_output(list(map(str, args)), text=True,
                                   stderr=subprocess.STDOUT, timeout=timeout).strip()


def wait_until(test, description, timeout=20):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = test()
        if value:
            return value
        time.sleep(0.2)
    raise RuntimeError(f"Timed out: {description}")


def descendants(pid):
    """Read only this browser's process tree, not other browser command lines."""
    found = []
    pending = [pid]
    while pending:
        parent = pending.pop()
        try:
            children = Path(f"/proc/{parent}/task/{parent}/children").read_text().split()
        except FileNotFoundError:
            continue
        for child in map(int, children):
            if child not in found:
                found.append(child)
                pending.append(child)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-receipt", type=Path, required=True)
    args = parser.parse_args()
    browser = None
    evidence = None
    result = {"automated_checks": [], "observed_checks": "pending screenshot review"}
    log = None
    try:
        receipt = json.loads(args.build_receipt.read_text())
        pins = tomllib.loads((ROOT / "mb/upstream-version.toml").read_text())
        if not isinstance(receipt, dict) or receipt.get("stage") != "build" or receipt.get("exit_code") != 0:
            raise RuntimeError("A successful upstream build receipt is required")
        if receipt.get("chromium_commit") != pins["chromium"]["commit"]:
            raise RuntimeError("Build receipt does not match the upstream pin")
        if receipt.get("depot_tools_commit") != pins["depot_tools"]["commit"]:
            raise RuntimeError("Build receipt does not match the depot_tools pin")
        if receipt.get("args_gn") != (ROOT / "mb/tools/gn/upstream-debug.gn").read_text():
            raise RuntimeError("Build receipt does not match the baseline arguments")
        if not os.access(BINARY, os.X_OK):
            raise RuntimeError(f"Browser binary is missing: {BINARY}")
        with BINARY.open("rb") as stream:
            binary_sha256 = hashlib.file_digest(stream, "sha256").hexdigest()
        if receipt.get("binary_sha256") != binary_sha256:
            raise RuntimeError("Browser binary hash does not match the successful build receipt")
        _, dependencies = upstream.verify()
        if receipt.get("git_dependencies") != dependencies:
            raise RuntimeError("Dependency revisions differ from the successful build receipt")
        if os.geteuid() == 0 or not os.environ.get("DISPLAY"):
            raise RuntimeError("Run as the desktop user with an accessible X11 DISPLAY")
        for tool in ("xdotool", "xprop", "xwininfo", "import"):
            if not shutil.which(tool):
                raise RuntimeError(f"Required X11 test tool is missing: {tool}")

        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        evidence = BUILD / "test-evidence" / f"upstream-{stamp}"
        profile = BUILD / "test-data" / f"upstream-{stamp}"
        evidence.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        profile.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        evidence.mkdir(mode=0o700)
        profile.mkdir(mode=0o700)
        print(f"Evidence: {evidence}", flush=True)
        result.update(build_receipt=str(args.build_receipt.resolve()),
                      binary=str(BINARY), profile=str(profile), display=os.environ["DISPLAY"])
        result["binary_sha256"] = binary_sha256
        argv = [str(BINARY), "--ozone-platform=x11", f"--user-data-dir={profile}",
                "--no-first-run", "--no-default-browser-check", "about:blank"]
        result["argv"] = argv
        log = (evidence / "browser.log").open("x")
        browser = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        result["pid"] = browser.pid

        def find_window():
            if browser.poll() is not None:
                raise RuntimeError(f"Browser exited during startup: {browser.returncode}")
            search = subprocess.run(
                ["xdotool", "search", "--onlyvisible", "--pid", str(browser.pid)],
                capture_output=True, text=True, timeout=5, check=False)
            for value in search.stdout.split():
                if not value.isdigit():
                    continue
                properties = command("xprop", "-id", value, "_NET_WM_PID", "_NET_WM_WINDOW_TYPE")
                if re.search(rf"_NET_WM_PID\(CARDINAL\) = {browser.pid}\b", properties) and "_NET_WM_WINDOW_TYPE_NORMAL" in properties:
                    return value
            return None

        window = wait_until(find_window, "test browser's visible X11 window")
        result["window"] = window
        (evidence / "window.txt").write_text(command("xwininfo", "-id", window) + "\n")

        def owned():
            if browser.poll() is not None:
                raise RuntimeError("Test browser has exited")
            properties = command("xprop", "-id", window, "_NET_WM_PID")
            if not re.search(rf"= {browser.pid}\b", properties):
                raise RuntimeError("Window no longer belongs to the test browser")

        def key(keys):
            owned()
            command("xdotool", "key", "--window", window, "--clearmodifiers", keys)

        def title():
            owned()
            return command("xprop", "-id", window, "_NET_WM_NAME", "WM_NAME")

        def navigate(url, expected):
            key("ctrl+l")
            command("xdotool", "type", "--window", window, "--clearmodifiers", "--delay", "1", url)
            key("Return")
            wait_until(lambda: expected in title(), f"page title contains {expected!r}")

        def capture(name):
            owned()
            time.sleep(0.5)
            command("import", "-window", window, evidence / f"{name}.png")
            (evidence / f"{name}-title.txt").write_text(title() + "\n")

        command("xdotool", "windowfocus", "--sync", window)
        capture("01-startup")
        first = "data:text/html," + quote("<title>Baseline one</title><h1>Navigation works</h1>")
        second = "data:text/html," + quote("<title>Baseline two</title><h1>Second tab</h1>")
        navigate(first, "Baseline one")
        result["automated_checks"].append("navigation")
        key("ctrl+t")
        navigate(second, "Baseline two")
        key("ctrl+shift+Tab")
        wait_until(lambda: "Baseline one" in title(), "previous tab")
        key("ctrl+Tab")
        wait_until(lambda: "Baseline two" in title(), "next tab")
        capture("02-tabs")
        key("ctrl+w")
        wait_until(lambda: "Baseline one" in title(), "close tab")
        key("ctrl+shift+t")
        wait_until(lambda: "Baseline two" in title(), "reopen tab")
        result["automated_checks"].append("create/select/close/reopen tabs by shortcuts")

        key("F12")
        time.sleep(2)
        capture("03-devtools-review-required")
        key("F12")
        navigate("chrome://sandbox", "Sandbox")
        capture("04-sandbox")
        processes = []
        for pid in descendants(browser.pid):
            try:
                proc = Path(f"/proc/{pid}")
                if proc.joinpath("exe").resolve() != BINARY.resolve():
                    continue
                cmd = proc.joinpath("cmdline").read_bytes().decode().split("\0")
                status = dict(line.split(":", 1) for line in proc.joinpath("status").read_text().splitlines())
                processes.append({"pid": pid, "renderer": "--type=renderer" in cmd,
                                  "status": {k: status.get(k, "").strip() for k in
                                             ("PPid", "NSpid", "NoNewPrivs", "Seccomp", "Seccomp_filters")}})
            except (FileNotFoundError, ProcessLookupError):
                continue
        result["processes"] = processes
        renderers = [proc for proc in processes if proc["renderer"]]
        if not renderers or any(proc["status"]["Seccomp"] != "2" or
                                proc["status"]["NoNewPrivs"] != "1" or
                                not proc["status"]["Seccomp_filters"].isdigit() or
                                int(proc["status"]["Seccomp_filters"]) < 1
                                for proc in renderers):
            raise RuntimeError("Renderer seccomp/no-new-privileges evidence is missing or unexpected")
        result["automated_checks"].append("live renderer seccomp filters and no-new-privileges")
        navigate("chrome://version", "Version")
        capture("05-version")
        key("ctrl+shift+w")
        exit_code = browser.wait(timeout=20)
        result["browser_exit_code"] = exit_code
        if exit_code != 0:
            raise RuntimeError(f"Browser exited abnormally: {exit_code}")
        result["automated_checks"].append("normal UI shutdown")
        result["automated_result"] = "passed"
        print("Automated assertions passed. Review screenshots before passing the baseline gate.")
        return 0
    except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as error:
        result["automated_result"] = "failed"
        result["error"] = str(error)
        print(f"error: {error}", file=sys.stderr)
        return 1
    finally:
        if browser is not None and browser.poll() is None:
            result["forced_cleanup"] = True
            browser.terminate()
            try:
                browser.wait(timeout=10)
            except subprocess.TimeoutExpired:
                browser.kill()
                browser.wait(timeout=10)
        if log is not None:
            log.close()
        if evidence is not None:
            (evidence / "results.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    sys.exit(main())

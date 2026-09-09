#!/usr/bin/env python3
"""Bounded two-environment explicit-config X11 smoke runner; never auto-runs."""
from __future__ import annotations
import argparse, hashlib, json, os, re, shutil, signal, subprocess, sys, tempfile, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "mb/tools"

def product_module():
    sys.path.insert(0, str(TOOLS))
    import product
    return product

def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

def validate_receipt(path):
    product = product_module()
    receipt = json.loads(path.read_text(encoding="utf-8"))
    if receipt.get("stage") != "build" or receipt.get("exit_code") != 0:
        raise RuntimeError("successful product build receipt required")
    verified = product.verify()
    if receipt.get("integration") != verified:
        raise RuntimeError("receipt integration differs from product.verify()")
    if receipt.get("integration_sha256") != sha(product.RECEIPT):
        raise RuntimeError("receipt integration hash differs from product receipt")
    values = verified.get("product", {})
    executable = values.get("executable_name")
    if not isinstance(executable, str) or not executable:
        raise RuntimeError("manifest executable missing")
    profile, binary = product.expected_product_binary(receipt, executable)
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise RuntimeError("product binary missing")
    if receipt.get("binary_sha256") != sha(binary):
        raise RuntimeError("product binary hash differs from receipt")
    args_path = profile["output"] / "args.gn"
    if not args_path.is_file() or args_path.read_text() != receipt.get("args_gn"):
        raise RuntimeError("product output args.gn differs from receipt")
    return receipt, binary, values

def config_text(data_roots):
    return f'''schema_version = 1

[app]
default_environment = "personal"
restore_last_environment = false

[environments.personal]
data_directory = "{data_roots['personal'].as_posix()}"
startup_urls = ["about:blank"]

[environments.work]
data_directory = "{data_roots['work'].as_posix()}"
startup_urls = ["about:blank"]
'''

def child_env(root):
    env = dict(os.environ)
    for key in tuple(env):
        if key.startswith("CHROME_EXTRA_FLAGS") or key in {"CHROME_USER_DATA_DIR", "CHROME_CONFIG_HOME"}:
            env.pop(key, None)
    env["TMPDIR"] = tempfile.mkdtemp(prefix="env-", dir=ROOT / ".build/tmp")
    env.update({"HOME": str(root / "home"), "XDG_CONFIG_HOME": str(root / "xdg-config"),
                "XDG_DATA_HOME": str(root / "xdg-data"), "XDG_STATE_HOME": str(root / "xdg-state"),
                "XDG_CACHE_HOME": str(root / "xdg-cache")})
    return env

def launch_args(binary, config, environment):
    args = [str(binary), "--ozone-platform=x11", "--no-first-run",
            "--no-default-browser-check", f"--config={config}",
            f"--environment={environment}", "about:blank"]
    if any("user-data-dir" in arg or "remote-debug" in arg for arg in args):
        raise RuntimeError("unsafe launch argument")
    return args

def wait_until(test, description, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = test()
        if value:
            return value
        time.sleep(0.2)
    raise RuntimeError(f"timed out waiting for {description}")

def peek(process):
    try:
        return os.waitid(os.P_PID, process.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
    except ChildProcessError as error:
        raise RuntimeError("lost owned process") from error

def wait_info(process, timeout):
    deadline = time.monotonic() + timeout
    while True:
        info = peek(process)
        if info is not None:
            return info
        if time.monotonic() >= deadline:
            raise TimeoutError("owned process timeout")
        time.sleep(0.05)

def kill_group(process):
    if process.returncode is not None:
        return {"owned": False, "reaped": True, "natural_exit": process.returncode}
    try:
        info = peek(process)
    except RuntimeError:
        return {"owned": False, "reaped": False}
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    if info is None:
        try:
            info = wait_info(process, 5)
        except TimeoutError:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                info = wait_info(process, 5)
            except (TimeoutError, RuntimeError):
                return {"owned": False, "reaped": False}
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5)
    except (subprocess.TimeoutExpired, ChildProcessError):
        return {"owned": False, "reaped": False}
    return {"owned": True, "reaped": True, "exit_code": process.returncode}

def singleton_locks(data_root):
    return sorted(path for path in data_root.rglob("SingletonLock")
                  if os.path.lexists(path))

def process_title(pid):
    return " ".join(part.decode(errors="replace") for part in
                     Path(f"/proc/{pid}/cmdline").read_bytes().rstrip(b"\0").split(b"\0"))

def x11_command(*args):
    result = subprocess.run(args, capture_output=True, text=True, timeout=5, check=False)
    if result.returncode:
        raise RuntimeError(f"X11 command failed: {' '.join(map(str, args))}")
    return result.stdout

def x11_windows(pid):
    if not shutil.which("xdotool") or not shutil.which("xprop"):
        raise RuntimeError("xdotool and xprop are required")
    result = subprocess.run(["xdotool", "search", "--onlyvisible", "--pid", str(pid)],
                            capture_output=True, text=True, timeout=5, check=False)
    windows = []
    for value in result.stdout.split():
        if value.isdigit():
            props = x11_command("xprop", "-id", value, "_NET_WM_PID", "_NET_WM_WINDOW_TYPE")
            if re.search(rf"_NET_WM_PID\(CARDINAL\) = {pid}\b", props) and "_NET_WM_WINDOW_TYPE_NORMAL" in props:
                windows.append(value)
    return windows

def focus_window(window):
    x11_command("xdotool", "windowactivate", str(window))
    x11_command("xdotool", "mousemove", "--window", str(window), "400", "20")
    x11_command("xdotool", "click", "1")
    if x11_command("xdotool", "getactivewindow").strip() != str(window):
        raise RuntimeError("owned window did not receive focus")

def close_window(window):
    # The target window manager does not reliably acknowledge EWMH activation.
    # Set X input focus directly, verify it, then send native XTEST accelerators.
    x11_command("xdotool", "windowfocus", "--sync", str(window))
    if x11_command("xdotool", "getwindowfocus").strip() != str(window):
        raise RuntimeError("owned browser did not receive X input focus")
    x11_command("xdotool", "key", "--clearmodifiers", "ctrl+shift+w")
    time.sleep(0.5)

def lock_owner(lock):
    target = os.readlink(lock)
    match = re.search(r"(?:^|[-_])(\d+)$", target)
    if not match:
        raise RuntimeError(f"SingletonLock owner is not a host-PID link: {lock}")
    return int(match.group(1)), target

def close_all_windows(process, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        windows = x11_windows(process.pid)
        if not windows:
            return
        for window in windows:
            close_window(window)
    raise TimeoutError("owned browser windows did not close")

def finish_naturally(process, timeout):
    wait_info(process, timeout)
    process.wait(timeout=5)
    if process.returncode != 0:
        raise RuntimeError(f"owned browser exited with {process.returncode}")

def run(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-receipt", type=Path, required=True)
    parser.add_argument("--evidence-parent", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args(argv)
    if os.geteuid() == 0 or not os.environ.get("DISPLAY"):
        raise RuntimeError("non-root X11 desktop required")
    if not 1 <= args.timeout <= 300:
        parser.error("--timeout must be 1..300")
    receipt, binary, product = validate_receipt(args.build_receipt.resolve())
    evidence = Path(tempfile.mkdtemp(prefix="two-env-", dir=args.evidence_parent.resolve()))
    evidence.chmod(0o700)
    roots = {}
    data_roots = {}
    for name in ("personal", "work"):
        root = evidence / ("fixture-" + name)
        root.mkdir(mode=0o700)
        for child in ("home", "xdg-config", "xdg-data", "xdg-state", "xdg-cache"):
            (root / child).mkdir(mode=0o700)
        roots[name] = root
        data_roots[name] = evidence / ("data-" + name)
    config = evidence / "config.toml"
    config.write_text(config_text(data_roots), encoding="utf-8")
    logs = {name: (evidence / f"{name}.log").open("wb") for name in roots}
    processes, windows, owned = {}, {}, []
    second_log = None
    result = {"status": "failed", "binary": str(binary), "binary_sha256": sha(binary),
              "evidence": str(evidence), "cleanup": []}
    try:
        for name, root in roots.items():
            process = subprocess.Popen(launch_args(binary, config, name), cwd=root,
                                       env=child_env(root), stdout=logs[name],
                                       stderr=subprocess.STDOUT, start_new_session=True)
            processes[name] = process
            owned.append(process)
            windows[name] = wait_until(lambda p=process: x11_windows(p.pid),
                                       f"{name} X11 window", args.timeout)
            if Path(f"/proc/{process.pid}/exe").resolve() != binary.resolve():
                raise RuntimeError(f"{name} process executable mismatch")
            title = process_title(process.pid)
            # Linux publishes its title before BasicStartupComplete injects the
            # root. Verify original selectors here and native singleton/root below.
            if f"--environment={name}" not in title or f"--config={config}" not in title:
                raise RuntimeError(f"{name} process title lacks explicit selectors")
            if "--remote-debug" in title:
                raise RuntimeError("unexpected remote debugging argument")
            if not (data_roots[name] / "Default").is_dir():
                raise RuntimeError("native profile absent from configured root")
            if name == "personal" and data_roots["work"].exists():
                raise RuntimeError("first environment created unselected root")
            properties = x11_command("xprop", "-id", windows[name][0], "WM_CLASS")
            if f'"{product["application_id"]}"' not in properties:
                raise RuntimeError(f"{name} WM_CLASS does not match the manifest application_id")
        locks = {name: wait_until(lambda r=data_roots[name]: singleton_locks(r),
                                  f"{name} SingletonLock", args.timeout)
                 for name in roots}
        if not locks["personal"] or not locks["work"] or set(map(str, locks["personal"])) & set(map(str, locks["work"])):
            raise RuntimeError("environment SingletonLocks are not distinct")
        for name, lock_paths in locks.items():
            if lock_owner(lock_paths[0])[0] != processes[name].pid:
                raise RuntimeError(f"{name} SingletonLock owner differs from process")
        owner_pid = processes["personal"].pid
        second_log = (evidence / "personal-second.log").open("wb")
        second = subprocess.Popen(launch_args(binary, config, "personal"), cwd=roots["personal"],
                                  env=child_env(roots["personal"]), stdout=second_log,
                                  stderr=subprocess.STDOUT, start_new_session=True)
        owned.append(second)
        finish_naturally(second, args.timeout)
        second_code = second.returncode
        second_log.close()
        if second_code != 0 or not Path(f"/proc/{owner_pid}").exists():
            raise RuntimeError("same-environment activation changed original owner")
        for name in ("personal", "work"):
            # Chromium's native POSIX handler dispatches chrome::SessionEnding.
            # Signal only the owned unreaped browser leader; let it stop children.
            if peek(processes[name]) is not None:
                raise RuntimeError("browser exited before requested session shutdown")
            os.kill(processes[name].pid, signal.SIGTERM)
            finish_naturally(processes[name], args.timeout)
            preferences = json.loads((data_roots[name] / "Default/Preferences").read_text())
            if preferences.get("profile", {}).get("exit_type") != "SessionEnded":
                raise RuntimeError("native session shutdown was not persisted")
        result.update({"status": "passed", "locks": {name: [str(path) for path in value] for name, value in locks.items()},
                       "same_environment_exit": second_code, "shutdown": "native POSIX SessionEnding; not keyboard close", "product": product})
    except (OSError, RuntimeError, TimeoutError, subprocess.SubprocessError) as error:
        result["error"] = str(error)
    finally:
        for process in owned:
            result["cleanup"].append(kill_group(process))
        for log in logs.values():
            log.close()
        if second_log is not None and not second_log.closed:
            second_log.close()
        if result["status"] == "passed" and any(item.get("owned") for item in result["cleanup"]):
            result["status"] = "failed"
            result["error"] = "success required no forced process cleanup"
        (evidence / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, sort_keys=True))
    return result

if __name__ == "__main__":
    try:
        raise SystemExit(0 if run()["status"] == "passed" else 1)
    except (OSError, RuntimeError, TimeoutError, subprocess.SubprocessError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)

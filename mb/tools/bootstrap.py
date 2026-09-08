#!/usr/bin/env python3
"""Preflight and fetch the pinned, unmodified Chromium source on Arch Linux."""

import argparse
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / ".build"
DEPOT = BUILD / "depot_tools"
CHECKOUT = BUILD / "chromium"
PIN = ROOT / "mb/upstream-version.toml"
# From docs/linux/build_instructions.md at the pinned release. Arch provides
# pkgconfig through pkgconf. git is additionally required for source fetching.
PACKAGES = (
    "git python perl gcc gcc-libs bison flex gperf pkgconf nss alsa-lib glib2 "
    "gtk3 nspr freetype2 cairo dbus xorg-server-xvfb xorg-xdpyinfo"
).split()


def run(*args, cwd=ROOT, env=None, capture=False):
    print(f"[{cwd}] {shlex.join(map(str, args))}", flush=True)
    result = subprocess.run(
        list(map(str, args)), cwd=cwd, env=env, check=True,
        text=True, stdout=subprocess.PIPE if capture else None,
    )
    return result.stdout.strip() if capture else None


def doctor():
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        raise RuntimeError("Initial build supports Linux x86_64 only")
    if not shutil.which("pacman"):
        raise RuntimeError("This preflight requires Arch Linux and pacman")
    missing = [p for p in PACKAGES if subprocess.run(
        ["pacman", "-Q", p], stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, check=False,
    ).returncode]
    free = shutil.disk_usage(ROOT).free / 2**30
    memory = {}
    for line in Path("/proc/meminfo").read_text().splitlines():
        key, value = line.split(":", 1)
        memory[key] = int(value.split()[0]) / 2**20
    print(f"Architecture: {platform.machine()}; logical CPUs: {os.cpu_count()}")
    print(f"Working disk free: {free:.1f} GiB")
    print(f"RAM total/available: {memory['MemTotal']:.1f}/{memory['MemAvailable']:.1f} GiB")
    print(f"Swap total/free: {memory['SwapTotal']:.1f}/{memory['SwapFree']:.1f} GiB")
    print(f"DISPLAY: {os.environ.get('DISPLAY', '(unset)')}")
    if memory["MemTotal"] < 16:
        print("WARNING: less than 16 GiB RAM; review RAM and swap before building")
    if free < 100:
        print("WARNING: less than 100 GiB free; review checkout/build space before continuing")
    if missing:
        print("Missing official Arch packages: " + " ".join(missing))
        print("User must install and confirm: sudo pacman -S --needed " + " ".join(missing))
        return False
    print("Required Arch packages are installed. No system changes performed.")
    return True


def ensure_repo(path, pin, ref):
    if not path.exists():
        path.mkdir(parents=True)
    if not (path / ".git").exists():
        if any(path.iterdir()):
            raise RuntimeError(f"Refusing to initialize a nonempty directory: {path}")
        run("git", "init", path)
        run("git", "remote", "add", "origin", pin["repository"], cwd=path)
    origin = run("git", "remote", "get-url", "origin", cwd=path, capture=True)
    if origin != pin["repository"]:
        raise RuntimeError(f"Unexpected origin in {path}: {origin}")
    head = subprocess.run(
        ["git", "rev-parse", "--verify", "HEAD"], cwd=path, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=False,
    )
    if head.returncode == 0:
        if head.stdout.strip() != pin["commit"]:
            raise RuntimeError(f"Refusing to move existing HEAD in {path}; review it manually")
        if run("git", "status", "--porcelain", "--untracked-files=normal", cwd=path, capture=True):
            raise RuntimeError(f"Refusing to sync a checkout with local changes: {path}")
        return
    run("git", "fetch", "--depth=1", "origin", ref, cwd=path)
    actual = run("git", "rev-parse", "FETCH_HEAD^{commit}", cwd=path, capture=True)
    if actual != pin["commit"]:
        raise RuntimeError(f"Pin mismatch for {path}: expected {pin['commit']}, got {actual}")
    run("git", "checkout", "--detach", actual, cwd=path)


def fetch():
    if not doctor():
        raise RuntimeError("System dependency gate: install the listed packages and confirm before fetching")
    if shutil.disk_usage(ROOT).free < 25 * 2**30:
        raise RuntimeError("Fetch/resume requires a 25 GiB free-space reserve; inspect disk usage")
    if not CHECKOUT.exists() and shutil.disk_usage(ROOT).free < 100 * 2**30:
        raise RuntimeError("Initial checkout needs at least 100 GiB free working space")
    pins = tomllib.loads(PIN.read_text())
    scratch = BUILD / "tmp"
    scratch.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PATH=f"{DEPOT}:{DEPOT / 'python-bin'}:{os.environ.get('PATH', '')}",
               DEPOT_TOOLS_UPDATE="0", DEPOT_TOOLS_METRICS="0", TMPDIR=str(scratch))
    ensure_repo(DEPOT, pins["depot_tools"], pins["depot_tools"]["commit"])
    # Disabling depot_tools auto-update also skips its normal Python bootstrap.
    # Invoke the checked-in bootstrap explicitly without moving the Git pin.
    run("bash", "-e", "-c", 'source "$1"; bootstrap_python3', "bootstrap-python",
        DEPOT / "bootstrap_python3", env=env)
    ensure_repo(CHECKOUT / "src", pins["chromium"], pins["chromium"]["tag"])
    config = (
        "solutions = [{\n"
        "    'name': 'src',\n"
        f"    'url': {pins['chromium']['repository']!r},\n"
        "    'managed': False,\n"
        "    'custom_deps': {},\n"
        "    'custom_vars': {},\n"
        "}]\n"
        "target_os = ['linux']\n"
    )
    gclient = CHECKOUT / ".gclient"
    if gclient.exists() and gclient.read_text() != config:
        raise RuntimeError(f"Refusing to overwrite a different {gclient}")
    if not gclient.exists():
        gclient.write_text(config)
    if shutil.disk_usage(ROOT).free < 25 * 2**30:
        raise RuntimeError("Less than 25 GiB free before dependency sync; inspect disk usage")
    run(DEPOT / "gclient", "sync", "--nohooks", "--no-history", "--jobs=4",
        "--revision", f"src@{pins['chromium']['commit']}", cwd=CHECKOUT, env=env)
    actual = run("git", "rev-parse", "HEAD", cwd=CHECKOUT / "src", capture=True)
    if actual != pins["chromium"]["commit"]:
        raise RuntimeError("Chromium HEAD changed unexpectedly during sync")
    print("Pinned source and dependencies fetched. Hooks and compilation have NOT run.")
    print("Next: inspect pinned hooks/build arguments, run hooks, build unmodified Chromium.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("doctor", "fetch"))
    args = parser.parse_args()
    try:
        if args.command == "doctor":
            return 0 if doctor() else 1
        fetch()
        return 0
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Build and run the standalone config tests outside Chromium's checkout."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
CHROMIUM = ROOT / ".build/chromium/src"
OUT = ROOT / ".build/config-tests"
PINNED_CLANG = CHROMIUM / "third_party/llvm-build/Release+Asserts/bin/clang++"


def compiler() -> str:
    if PINNED_CLANG.is_file() and os.access(PINNED_CLANG, os.X_OK):
        return str(PINNED_CLANG)
    cxx = shutil.which("g++")
    if not cxx:
        raise RuntimeError("Pinned Chromium clang++ is unavailable and system g++ was not found")
    print("note: pinned Chromium clang++ is unavailable; using system g++ fallback", file=sys.stderr)
    return cxx


def main() -> int:
    gtest = CHROMIUM / "third_party/googletest/src/googletest"
    if not (gtest / "src/gtest-all.cc").is_file():
        raise RuntimeError(f"missing pinned GoogleTest source: {gtest}")
    OUT.mkdir(parents=True, exist_ok=True)
    temporary = OUT / "tmp"
    temporary.mkdir(mode=0o700, exist_ok=True)
    binary = OUT / "config_test"
    command = [
        compiler(), "-std=c++20", "-fno-exceptions", "-fno-rtti", "-pthread",
        "-Wall", "-Wextra", "-Werror",
        "-DTOML_EXCEPTIONS=0", "-I", str(ROOT), "-I", str(gtest / "include"),
        "-I", str(gtest), str(ROOT / "mb/config/config.cc"),
        str(ROOT / "mb/test/config_test.cc"), str(gtest / "src/gtest-all.cc"),
        str(gtest / "src/gtest_main.cc"), "-o", str(binary),
    ]
    print("+", " ".join(command), flush=True)
    subprocess.run(command, check=True, cwd=ROOT,
                   env=dict(os.environ, TMPDIR=str(temporary)))
    print("+", binary, flush=True)
    return subprocess.run([str(binary), f"--gtest_output=xml:{OUT / 'results.xml'}"],
                          check=False, cwd=ROOT).returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)

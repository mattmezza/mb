#!/usr/bin/env python3
"""Compile and run the standalone runtime-configuration tests."""

from pathlib import Path
import os
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / ".build/chromium/src"
OUTPUT = ROOT / ".build/runtime-config-tests"


def main() -> int:
    compiler = SOURCE / "third_party/llvm-build/Release+Asserts/bin/clang++"
    gtest = SOURCE / "third_party/googletest/src/googletest"
    required = (compiler, gtest / "src/gtest-all.cc", gtest / "src/gtest_main.cc")
    if not all(path.is_file() for path in required):
        print("Missing pinned Chromium clang++ or GoogleTest source", file=sys.stderr)
        return 2
    OUTPUT.mkdir(parents=True, exist_ok=True)
    temporary = OUTPUT / "tmp"
    temporary.mkdir(mode=0o700, exist_ok=True)
    binary = OUTPUT / "runtime_config_test"
    command = [
        str(compiler), "-std=c++20", "-fno-exceptions", "-fno-rtti",
        "-Wall", "-Wextra", "-Werror", "-pthread", "-g", "-O1",
        "-DTOML_EXCEPTIONS=0", "-DGTEST_HAS_EXCEPTIONS=0", "-DGTEST_HAS_RTTI=0",
        "-I", str(ROOT), "-isystem", str(gtest / "include"), "-I", str(gtest),
        str(ROOT / "mb/config/config.cc"), str(ROOT / "mb/config/config_file.cc"),
        str(ROOT / "mb/browser/environment_paths.cc"),
        str(ROOT / "mb/config/runtime_config.cc"),
        str(ROOT / "mb/test/runtime_config_test.cc"),
        str(gtest / "src/gtest-all.cc"), str(gtest / "src/gtest_main.cc"),
        "-o", str(binary),
    ]
    environment = dict(os.environ, TMPDIR=str(temporary),
                       MB_RUNTIME_CONFIG_TEST_TMPDIR=str(temporary))
    subprocess.run(command, check=True, cwd=ROOT, env=environment)
    return subprocess.run(
        [str(binary), f"--gtest_output=xml:{OUTPUT / 'results.xml'}"],
        check=False, cwd=ROOT, env=environment).returncode


if __name__ == "__main__":
    sys.exit(main())

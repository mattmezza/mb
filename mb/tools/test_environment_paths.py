#!/usr/bin/env python3
"""Compile/run the standalone C++ core using checkout-pinned clang and GoogleTest."""

import os
from pathlib import Path
import subprocess
import sys


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    source = root / ".build/chromium/src"
    compiler = source / "third_party/llvm-build/Release+Asserts/bin/clang++"
    gtest = source / "third_party/googletest/src/googletest"
    output = root / ".build/environment-tests"
    output.mkdir(parents=True, exist_ok=True)
    temporary = output / "tmp"
    temporary.mkdir(mode=0o700, exist_ok=True)
    binary = output / "environment_paths_test"
    for required in (compiler, gtest / "src/gtest-all.cc", gtest / "src/gtest_main.cc"):
        if not required.is_file():
            print(f"Missing pinned test dependency: {required}", file=sys.stderr)
            return 2
    command = [
        str(compiler), "-std=c++20", "-fno-exceptions", "-fno-rtti",
        "-Wall", "-Wextra", "-Werror", "-pthread", "-g", "-O1",
        "-DGTEST_HAS_EXCEPTIONS=0", "-DGTEST_HAS_RTTI=0",
        "-I", str(root), "-I", str(gtest), "-I", str(gtest / "include"),
        str(root / "mb/browser/environment_paths.cc"),
        str(root / "mb/test/environment_paths_test.cc"),
        str(gtest / "src/gtest-all.cc"), str(gtest / "src/gtest_main.cc"),
        "-o", str(binary),
    ]
    environment = os.environ.copy()
    environment["TMPDIR"] = str(temporary)
    environment["MB_ENVIRONMENT_TEST_TMPDIR"] = str(temporary)
    subprocess.run(command, check=True, cwd=root, env=environment)
    return subprocess.run([str(binary), f"--gtest_output=xml:{output / 'results.xml'}",
                           *sys.argv[1:]], cwd=root,
                          env=environment, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())

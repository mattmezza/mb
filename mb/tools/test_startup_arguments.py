#!/usr/bin/env python3
"""Compile and run the standalone startup argument tests using pinned tools."""

from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / ".build/chromium/src"
OUTPUT = ROOT / ".build/startup-tests"


def main():
    compiler = SOURCE / "third_party/llvm-build/Release+Asserts/bin/clang++"
    gtest = SOURCE / "third_party/googletest/src/googletest"
    if not compiler.is_file() or not (gtest / "src/gtest-all.cc").is_file():
        raise SystemExit("Fetch the pinned Chromium checkout and run hooks first")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    binary = OUTPUT / "startup_arguments_test"
    command = [str(compiler), "-std=c++20", "-fno-exceptions", "-fno-rtti",
               "-Wall", "-Wextra", "-Werror", "-pthread", "-I", str(ROOT),
               "-isystem", str(gtest / "include"), "-I", str(gtest),
               str(ROOT / "mb/app/startup_arguments.cc"),
               str(ROOT / "mb/test/startup_arguments_test.cc"),
               str(gtest / "src/gtest-all.cc"), str(gtest / "src/gtest_main.cc"),
               "-o", str(binary)]
    subprocess.run(command, check=True)
    subprocess.run([str(binary), f"--gtest_output=xml:{OUTPUT / 'results.xml'}"], check=True)


if __name__ == "__main__":
    main()

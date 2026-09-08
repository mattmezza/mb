#!/usr/bin/env python3
"""Build the standalone control command without modifying the Chromium checkout."""

from pathlib import Path
import os
import subprocess

import branding

ROOT = Path(__file__).resolve().parents[2]


def build():
    compiler = ROOT / ".build/chromium/src/third_party/llvm-build/Release+Asserts/bin/clang++"
    if not compiler.is_file():
        raise SystemExit("Fetch pinned Chromium and run hooks first")
    generated = branding.generate(branding.DEFAULT_MANIFEST, branding.DEFAULT_OUTPUT)
    identity = branding.load_manifest(branding.DEFAULT_MANIFEST)["product"]
    output = ROOT / ".build/control"
    output.mkdir(parents=True, exist_ok=True)
    temporary = output / "tmp"
    temporary.mkdir(mode=0o700, exist_ok=True)
    environment = dict(os.environ, TMPDIR=str(temporary))
    binary = output / (identity["executable_name"] + "ctl")
    sources = ["mb/app/control_main.cc", "mb/app/startup_arguments.cc",
               "mb/config/config.cc", "mb/config/config_file.cc",
               "mb/browser/environment_paths.cc"]
    subprocess.run([str(compiler), "-std=c++20", "-fno-exceptions", "-fno-rtti",
                    "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                    "-I", str(generated), *[str(ROOT / source) for source in sources],
                    "-o", str(binary)], check=True, env=environment)
    print(binary, flush=True)
    return binary


if __name__ == "__main__":
    build()

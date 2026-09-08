#!/usr/bin/env python3
"""Run focused standalone product checks with logs and an explicit receipt."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SUITES = {
    "upstream-tools": [["-m", "unittest", "mb.test.test_upstream_tools", "mb.test.test_upstream_smoke", "mb.test.test_check_upstream", "-v"]],
    "branding": [["-m", "unittest", "mb.test.test_branding", "mb.test.test_branding_assets", "mb.test.test_branding_strings", "-v"]],
    "startup": [["mb/tools/test_startup_arguments.py"]],
    "config": [["mb/tools/test_config.py"]],
    "environments": [["mb/tools/test_environment_paths.py"]],
    "runtime": [["mb/tools/test_runtime_config.py"]],
    "control": [["mb/tools/build_control.py"], ["-m", "unittest", "mb.test.test_control", "-v"]],
    "fixtures": [["-m", "unittest", "mb.test.test_fixture_server", "-v"]],
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", action="append", choices=SUITES,
                        help="run only this suite; repeat to select several")
    parser.add_argument("--list", action="store_true", help="print commands without running them")
    args = parser.parse_args()
    selected = list(dict.fromkeys(args.suite or SUITES))
    commands = [(suite, [sys.executable, *command])
                for suite in selected for command in SUITES[suite]]
    if args.list:
        print(json.dumps(commands, indent=2))
        return 0
    source = ROOT / ".build/chromium/src"
    if any(suite not in {"upstream-tools", "fixtures"} for suite in selected):
        for required in ("tools/grit/grit.py", "third_party/googletest/src/googletest/src/gtest.cc",
                         "third_party/llvm-build/Release+Asserts/bin/clang++"):
            if not (source / required).is_file():
                parser.error(f"fetch the pinned checkout and run hooks first: missing {required}")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    evidence = ROOT / ".build/test-evidence" / f"standalone-{stamp}"
    evidence.mkdir(parents=True, mode=0o700)
    receipt = {"schema_version": 1, "kind": "standalone-product-tests",
               "started": stamp, "browser_acceptance": "not evaluated", "steps": []}
    result_path = evidence / "results.json"
    print(f"Evidence: {evidence}", flush=True)
    for index, (suite, command) in enumerate(commands):
        path = evidence / f"{index:02d}-{suite}.log"
        step = {"suite": suite, "command": command, "log": path.name,
                "exit_code": None}
        receipt["steps"].append(step)
        result_path.write_text(json.dumps(receipt, indent=2) + "\n")
        print(f"Running {suite}: {path.name}", flush=True)
        with path.open("x") as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log,
                                    stderr=subprocess.STDOUT, check=False)
        step["exit_code"] = result.returncode
        receipt["finished"] = datetime.now(timezone.utc).isoformat()
        result_path.write_text(json.dumps(receipt, indent=2) + "\n")
        if result.returncode:
            print(f"Failed: {suite}; inspect {path}", file=sys.stderr)
            return 1
    print(f"Selected standalone checks passed: {result_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

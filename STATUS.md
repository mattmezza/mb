# Implementation status

Updated: 2026-09-08.

- Current phase: Phase 1; hooks and GN generation passed; upstream build running.
- Last successful browser build: none.
- Last successful browser test: none.
- Last successful tests: `python3 -m unittest mb.test.test_upstream_tools
  mb.test.test_branding -v`; all 23 tests passed on 2026-09-08, including
  generated C++20/version-header compilation and actual pinned GN evaluation.
- Last successful tooling checks: branding generation and
  `desktop-file-validate .build/generated/branding/mb.desktop` passed.
- Standalone startup argument tests: all eight GoogleTests passed via
  `python3 mb/tools/test_startup_arguments.py`; pinned Clang, C++20,
  exceptions/RTTI disabled, warnings treated as errors.
- Upstream: Chromium Linux stable 152.0.7977.82,
  `d04cdb24d67b081f6cf80200ffc5233f44b61109`, independently matched using Gitiles
  and `git ls-remote` against its stable tag.
- Source checkout: pinned Chromium and gclient dependency sync completed
  successfully (fetch-01.log, exit 0). Chromium modifications: none.
- Tool bootstrap: pinned depot_tools Python 3.11.8 installed and verified;
  depot_tools HEAD remains pinned and tracked source is clean.
- Blockers: none currently. User confirmed gperf installation; preflight passes.
- Hooks passed: `.build/logs/upstream-hooks-20260908T075415.123263Z.json`.
- GN generation passed: `.build/logs/upstream-gen-20260908T080419.796335Z.json`;
  no unused GN arguments. Both stages verified all 165 nested Git dependencies.
- Active operation: `python3 mb/tools/upstream.py build --jobs 8`; timestamped
  log and receipt under `.build/logs/upstream-build-*`.
- Exact compile: pinned `autoninja -C out/mb-debug -j 8 chrome` from Chromium src.
- Exact arguments: `mb/tools/gn/upstream-debug.gn`, copied unchanged to
  `.build/chromium/src/out/mb-debug/args.gn`: debug component build, symbols 1,
  Blink/V8 symbols 0, local Siso, X11 enabled, unbranded. Security/codec defaults
  remain upstream. Pre-build free disk: about 113 GiB; available RAM: 14 GiB.
- First compile intentionally interrupted after 40m54s to increase jobs from
  four to eight: 9,901 completed actions, zero failed compilation actions,
  61,982 remaining. Siso flushed its state normally. Receipt/log
  `upstream-build-20260908T080510.178925Z` records the interruption (exit 1).
  Measured before resuming: 111 GiB disk free, 12 GiB RAM available, low memory
  pressure, four compiler processes using about 100–150 MiB each. Resume uses
  completed outputs and the updated tool that hashes the successful binary.
- Independent preparation: branding manifest/generator reviewed and tested.
  Configuration parser: 15 GoogleTests pass, including a depth-limit regression.
  Directory core: 22 GoogleTests pass. Compiled control command: 17 end-to-end
  tests pass for validation, XDG location, environment listing/path inspection,
  invalid files, permissions, and no environment-data creation. Build command:
  `python3 mb/tools/build_control.py`; tests: `python3 -m unittest
  mb.test.test_control -v`. All C++ is tested with exceptions/RTTI disabled.
  Nothing has been integrated into Chromium. The X11 smoke driver is prepared
  but has not launched a browser. Local MV3 extension fixture prepared and
  syntax-checked with Node; runtime behavior is pending.
- GitHub: `https://github.com/mattmezza/mb.git`; standalone product libraries,
  control command, and capability fixtures pushed through `d784528` on main.
  Source and build artifacts remain ignored.

## Exact continuation

From this repository, inspect the active stage/log before retrying:

```sh
python3 mb/tools/bootstrap.py doctor
python3 mb/tools/bootstrap.py fetch
python3 mb/tools/upstream.py hooks
python3 mb/tools/upstream.py gen
python3 mb/tools/upstream.py build --jobs 8
```

Pinned hooks and GN definitions have been inspected. Run stages sequentially,
then launch on Xorg with
sandbox enabled using docs/testing.md. No product
patches are allowed before that gate passes. Fetch and build logs must be kept
under `.build/`, and this file updated before each long-running build.

## Remaining work

All browser acceptance criteria are pending. Follow
[the phase plan](docs/implementation-plan.md): upstream build and launch, central
branding, native vertical tabs, TOML configuration and isolated environments,
capability verification, release build and tested Arch packaging. Maintain the
remaining product documentation as each implementation becomes concrete.

Resource watch: initial disk free 146 GiB; RAM 30 GiB with 14 GiB available;
swap 47 GiB with 28 GiB free. `/tmp` is nearly full tmpfs; use `.build/tmp`.
Two builds plus browser-test targets may need more space than the initial
checkout/build; recheck before each milestone. No unrelated files were removed.

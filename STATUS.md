# Implementation status

Updated: 2026-09-08.

- Current phase: Phase 1; hooks and GN generation passed; upstream build running.
- Last successful browser build: none.
- Last successful browser test: none.
- Last successful tests: `python3 -m unittest mb.test.test_upstream_tools
  mb.test.test_branding -v`; all 19 tests passed on 2026-09-08 (1.617 seconds),
  including generated C++20 header compilation and pinned GN evaluation.
- Last successful tooling checks: branding generation and
  `desktop-file-validate .build/generated/branding/mb.desktop` passed.
- Standalone startup argument tests: all seven GoogleTests passed via
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
- Active operation: `python3 mb/tools/upstream.py build --jobs 4`; timestamped
  log and receipt under `.build/logs/upstream-build-*`.
- Exact compile: pinned `autoninja -C out/mb-debug -j 4 chrome` from Chromium src.
- Exact arguments: `mb/tools/gn/upstream-debug.gn`, copied unchanged to
  `.build/chromium/src/out/mb-debug/args.gn`: debug component build, symbols 1,
  Blink/V8 symbols 0, local Siso, X11 enabled, unbranded. Security/codec defaults
  remain upstream. Pre-build free disk: about 113 GiB; available RAM: 14 GiB.
- First build is running the earlier stage-tool code loaded at launch. Once it
  succeeds, rerun the incremental build once with the updated tool to produce
  the binary SHA-256 receipt required by `upstream_smoke.py`. This also checks
  that a second build converges without source changes.
- Independent preparation: branding manifest/generator reviewed and tested.
  Standalone configuration and environment-path libraries are in development.
  Nothing has been integrated into Chromium. The X11 smoke driver is prepared
  but has not launched a browser.
- GitHub: `https://github.com/mattmezza/mb.git`; reviewed setup checkpoint
  `fa982e0` pushed to main. Source and build artifacts remain ignored.

## Exact continuation

From this repository, inspect the active stage/log before retrying:

```sh
python3 mb/tools/bootstrap.py doctor
python3 mb/tools/bootstrap.py fetch
python3 mb/tools/upstream.py hooks
python3 mb/tools/upstream.py gen
python3 mb/tools/upstream.py build --jobs 4
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

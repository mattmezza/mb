# Implementation status

Updated: 2026-09-08.

- Current phase: Phase 1, unmodified Chromium baseline compilation.
- Last successful browser build: none.
- Last successful browser test: none. All twenty browser acceptance criteria remain open.
- Last successful product test: all 107 standalone tests passed with pinned Python
  on 2026-09-08. Command: `.build/depot_tools/python-bin/python3 mb/tools/test_product.py`.
  Receipt and per-command logs: `.build/test-evidence/standalone-20260908T100704.408098Z/results.json`.
- Blockers: none currently. Dependencies, including user-installed gperf, are verified.
- Repository: reviewed preparation is pushed to `https://github.com/mattmezza/mb`
  on `main`. Source, binaries, logs and test data remain under ignored `.build/`.
- Chromium modifications: none. Product integration is gated on successful
  unmodified compilation and an observed sandboxed launch on the actual Xorg session.

## Active build

```sh
python3 mb/tools/upstream.py build --jobs 8
```

This command is already running; inspect its log before starting another build:
`.build/logs/upstream-build-20260908T084625.389068Z.log`.
It runs pinned `autoninja -C out/mb-debug -j 8 chrome` from `.build/chromium/src`.
The successful receipt will be written beside the log and include the binary hash.

Pinned Linux stable: Chromium 152.0.7977.82,
`d04cdb24d67b081f6cf80200ffc5233f44b61109`. Source and all 165 Git dependencies
were verified before compilation. Hooks passed in
`upstream-hooks-20260908T075415.123263Z.json`; GN generation passed in
`upstream-gen-20260908T080419.796335Z.json`, both under `.build/logs/`.
Arguments are exactly `mb/tools/gn/upstream-debug.gn`: debug component build,
symbols 1, Blink/V8 symbols 0, local Siso, X11 enabled, unbranded upstream defaults.

The first four-job run was intentionally interrupted after 40m54s, with 9,901
completed actions and no compiler failures, to resume at eight jobs. Its exit-1
receipt is `upstream-build-20260908T080510.178925Z.json`; it is not a successful
build receipt. Completed outputs were preserved.

Latest resource check: approximately 105 GiB free disk, 9.7 GiB available RAM,
28 GiB unused swap, low memory pressure. Total RAM is 30.8 GiB and total swap
47 GiB. The laptop is on AC power; CPU policy caps performance cores at 2 GHz.
Use `.build/tmp` for build scratch. Recheck resources before each milestone.

## Prepared and tested outside Chromium

- Central branding manifest, native headers/GN/BRANDING/GRIT derivation, desktop
  metadata and original temporary icons. GRIT retains 680 resource IDs,
  unchanged translations and upstream attribution. No custom-scheme handler is registered.
- Startup argument normalization; bounded strict TOML parser; safe file loading;
  audited environment paths and private directory creation; runtime selection snapshot.
- Compiled configuration companion: validation, environment listing and path lookup.
  Remembered-name persistence and browser configuration consumption remain pending.
- Loopback capability pages and an unpacked MV3 fixture. Extension JavaScript is
  syntax-checked; no extension runtime behavior has been verified.
- PID-scoped Xorg smoke driver with executable, process-title and kernel sandbox
  checks. It has not launched a browser; screenshots require explicit visual review.

See [testing](docs/testing.md) for individual checks and
[known limitations](docs/known-limitations.md) for remaining capability limits.

## Exact next work

After the active compile succeeds, verify the successful receipt and run:

```sh
python3 mb/tools/upstream_smoke.py --build-receipt .build/logs/upstream-build-20260908T084625.389068Z.json
```

Inspect the resulting screenshots and record navigation, DevTools, browser-reported
sandbox state and clean shutdown in that run's `results.md`. Only then integrate
product branding and compile/launch the renamed browser. Continue with native
vertical tabs, configuration/environments, capability verification, release build,
Arch packaging and update documentation following [the written plan](docs/implementation-plan.md).

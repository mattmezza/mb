# Implementation status

Updated: 2026-09-08.

- Current phase: Phase 1, unmodified Chromium baseline compilation.
- Last successful browser build: none.
- Last successful browser test: none. All twenty browser acceptance criteria remain open.
- Last successful product test: all 107 standalone tests passed with pinned Python
  on 2026-09-08. Command: `.build/depot_tools/python-bin/python3 mb/tools/test_product.py`.
  Receipt and per-command logs: `.build/test-evidence/standalone-20260908T100704.408098Z/results.json`.
  Subsequent media addition: five fixture tests pass; the original VP8 test
  clip is deterministic and all 48 frames decode with FFmpeg. Browser playback is pending.
  Latest focused run: 21 upstream-tool tests passed, including 12 candidate-checker
  tests, in `.build/test-evidence/standalone-20260908T111509.757071Z/results.json`.
  Live candidate verification matches the current pin; evidence is under
  `.build/update-checks/20260908T111527.423218Z/`.
- Blockers: none currently. Dependencies, including user-installed gperf, are verified.
- Repository: reviewed preparation is pushed to `https://github.com/mattmezza/mb`
  on `main`. Source, binaries, logs and test data remain under ignored `.build/`.
- Chromium modifications: none. Product integration is gated on successful
  unmodified compilation and an observed sandboxed launch on the actual Xorg session.

## Active build

```sh
python3 mb/tools/upstream.py build --jobs 12
```

This command is already running; inspect its log before starting another build:
`.build/logs/upstream-build-20260908T112027.558220Z.log`.
It runs pinned `autoninja -C out/mb-debug -j 12 chrome` from `.build/chromium/src`.
The successful receipt will be written beside the log and include the binary hash.

Checkpoint 2026-09-08 12:37 UTC: the twelve-job run has completed
more than 3,000 actions and is compiling Blink core. The active log has no
compiler failure markers. Source remains unmodified; no browser binary has
completed its build or launch gate.

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
build receipt. The eight-job run was then intentionally interrupted after 2h19m28s,
with 17,864 additional completed actions, zero compiler failures and 33,098 remaining,
to resume at ten jobs after the heavy V8 compilation finished. Its exit-1 receipt is
`upstream-build-20260908T084625.389068Z.json`. The ten-job run completed 1,933
more actions in 14m08s, with zero compiler failures, before the final increase to
twelve jobs. Its intentional-interruption receipt is
`upstream-build-20260908T110604.504034Z.json`. Completed outputs were preserved.

Latest resource check: approximately 98.8 GiB free disk, 7 GiB available RAM,
25 GiB unused swap, low memory pressure. Total RAM is 30.8 GiB and total swap
47 GiB. The laptop is on AC power; CPU policy caps performance cores at 2 GHz.
The requested 100 GiB free-space warning threshold has been crossed during
compilation; the user was informed. Capacity is not currently blocking this
already-fetched build. Use `.build/tmp` for build scratch and continue monitoring.
Recheck resources before each milestone, especially a second release output.

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
- An unrelated root-level `test/pages/README.md` appeared during work; its
  provenance is unconfirmed. It is preserved and excluded from product commits.
- PID-scoped Xorg smoke driver with executable, process-title and kernel sandbox
  checks. It has not launched a browser; screenshots require explicit visual review.
- Read-only stable candidate checker and upstream update procedure. Packaging
  must generate real credits and explicitly build/install the sandbox helper;
  those product integrations and the release/package rehearsal remain pending.

See [testing](docs/testing.md) for individual checks and
[known limitations](docs/known-limitations.md) for remaining capability limits.

## Exact next work

After the active compile succeeds, verify the successful receipt and run:

```sh
python3 mb/tools/upstream_smoke.py --build-receipt .build/logs/upstream-build-20260908T112027.558220Z.json
```

Inspect the resulting screenshots and record navigation, DevTools, browser-reported
sandbox state and clean shutdown in that run's `results.md`. Only then integrate
product branding and compile/launch the renamed browser. Continue with native
vertical tabs, configuration/environments, capability verification, release build,
Arch packaging and update documentation following [the written plan](docs/implementation-plan.md).

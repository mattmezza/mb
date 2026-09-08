# Implementation status

Updated: 2026-09-08.

- Current phase: Phase 2, product branding integration.
- Last successful browser build: unmodified Chromium 152.0.7977.82, 2026-09-08 18:05 UTC.
  Receipt: `.build/logs/upstream-build-20260908T112027.558220Z.json`.
- Last successful browser test: actual Xorg baseline passed on 2026-09-08.
  Evidence: `.build/test-evidence/upstream-20260908T181043.265925Z/review.json`
  and `results.md`; navigation, tabs, docked DevTools, sandbox and clean exit observed.
  Product acceptance criteria remain open.
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
- Chromium modifications: Phase 2 narrow branding patches are now being authored,
  following the successful unmodified build and reviewed sandboxed Xorg launch.

## Successful upstream build

```sh
python3 mb/tools/upstream.py build --jobs 12
```

This command finished successfully; retained log:
`.build/logs/upstream-build-20260908T112027.558220Z.log`.
It runs pinned `autoninja -C out/mb-debug -j 12 chrome` from `.build/chromium/src`.
The successful receipt is beside the log and includes the binary hash.

Final twelve-job run: 6h45m16s, 26,086 completed steps, build succeeded.
Earlier intentionally interrupted runs retained their completed outputs.
Full review and prior smoke failures: [baseline record](docs/upstream-baseline-2026-09-08.md).

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

Latest resource check after build: approximately 85 GiB free disk, 15 GiB
available RAM and 26 GiB unused swap. Total RAM is 30.8 GiB and total swap 47 GiB.
The requested 100 GiB disk warning was reported. Capacity permits incremental
development; recheck before a separate release output. A focused product test build is running; see below.

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
  checks. Baseline passed with explicit screenshot review; broader capability checks remain.
- Read-only stable candidate checker and upstream update procedure. Packaging
  must generate real credits and explicitly build/install the sandbox helper;
  those product integrations and the release/package rehearsal remain pending.

See [testing](docs/testing.md) for individual checks and
[known limitations](docs/known-limitations.md) for remaining capability limits.

## Active Phase 2 work and exact continuation

Product overlay and generated identity are staged. GN generation with
`--fail-on-unused-args` and `gn check out/mb-debug '//mb:*'` passed.
Product test build passed:
`.build/logs/product-test-build-20260908T182621.251698Z.json`.
All 51 GN-built C++ tests passed; evidence:
`.build/test-evidence/product-unit-20260908T182711.363809Z/`.
All 17 companion end-to-end tests passed against the GN-built executable.
Eight overlay tests and fifteen branding tests also passed.

The resource-ID fix is implemented and tested: generated GRDs alias the
GN-resolved allocation. Five resolver tests include identical upstream/derived
headers with the actual map. Preparation, GN generation and the reproducible
51-C++/17-companion test wrapper passed. The renamed browser build is running:
`.build/logs/product-build-20260908T184734.627427Z.log`.
Unified build session: 60192. Do not start a concurrent build.

Reproduction commands:

```sh
python3 mb/tools/product.py prepare
python3 mb/tools/product.py gen
python3 mb/tools/product.py test --jobs 12
python3 mb/tools/product.py build --jobs 12
```

Last failed run: `.build/logs/product-build-20260908T183317.833680Z.log`,
1,103 completed actions and 1,404 pending. Completed outputs are retained. It builds `chrome` (output executable derived from the manifest) and
`chrome_sandbox`, incrementally reusing `out/mb-debug`. Product generation receipt:
`.build/logs/product-gen-20260908T182003.628827Z.json`.

Next: review the browser build receipt and run a scoped sandboxed Xorg smoke
against the renamed binary. Then native vertical tabs, local-only new-tab UI,
configuration/environments, capability verification, release and Arch packaging.

Integration fixes: product buffer accesses use bounded containers; the C runtime
argv array has one documented span conversion. The vendored toml++ header has a
scoped unsafe-buffer diagnostic exception; parser limits and runtime hardening
remain enabled. No sandbox, certificate, origin or site-isolation protection was disabled.

Known upstream issue: percent-encoded data-URL typing triggered a debug omnibox
sanitization DCHECK. Base64 fixtures passed; the defect remains recorded for
regression work. F12 and Ctrl+T have not yet passed verification; the baseline
used Ctrl+Shift+I and direct URL new-tab creation with Alt+Enter.

The overlay review also fixed two maintenance cases: retiring an upstream patch
restores the pinned source file; retiring generated output archives only owned
files. Verification now detects deletion of a root product source or patch.
The product smoke wrapper is prepared and unit-tested but has not run. After
all tool changes are staged, use a fresh successful build receipt for its check.

Latest GN/test receipts: `product-gen-20260908T184710.256676Z.json` and
`product-test-build-20260908T184721.835515Z.json` under `.build/logs/`.
Reproducible test evidence: `.build/test-evidence/product-unit-20260908T184731.229729Z/`.
Real credits include toml++. The current browser build already generated the
resolved ID aliases successfully; locale repacking and final linking are pending.

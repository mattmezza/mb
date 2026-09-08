# Implementation status

Updated: 2026-09-08.

- Current phase: Phase 3, custom native UI. Phase 2 branding launch passed.
- Last successful browser build: branded debug mb, receipt
  `.build/logs/product-build-20260908T190956.375119Z.json`.
  First full product build passed in 10m31s (559 final incremental steps):
  `.build/logs/product-build-20260908T184734.627427Z.json`.
- Last successful browser test: sandboxed actual Xorg product launch, navigation,
  tab shortcuts, docked DevTools Elements, sandbox, manifest identity and clean exit.
  Reviewed evidence: `.build/test-evidence/product-20260908T191032.883440Z/review.json`.
  See [product review](docs/product-baseline-2026-09-08.md) for scope and prior failures.
- Last successful product core tests: 51 Chromium-built C++ tests and 17 companion
  end-to-end checks; `.build/test-evidence/product-unit-20260908T184731.229729Z/`.
  Branding (15), integration (8), resolved resource IDs (5), smoke helpers (3)
  and loopback/media fixtures (5) also passed focused checks. Earlier full
  standalone run: 107 passed, `standalone-20260908T100704.408098Z/results.json`.
- Blockers: none currently; installed dependencies verified. No sudo was run.
- Resources: approximately 81 GiB free disk, 17 GiB available RAM, 25 GiB unused
  swap. The requested below-100-GiB disk warning was reported. Incremental work
  fits; recheck capacity before a separate release output or large test build.
- Repository remote: `git@github.com:mattmezza/mb.git`, branch `main`.
  Ignored `.build/` holds source, binaries, logs and private test data.
  Unrelated root `test/pages/README.md` and `.tmux-session` remain untouched.

## Exact pending work

Prepare and review a bundled local-only NTP controller, preserving genuine OTR
and extension override behavior; build and test before native sidebar changes.
Add a focused real-browser test executable and tab operation tests. Then wire
product window/command/sidebar composition to Chromium's authoritative model,
including pinned/grouped/dragged tabs, status, accessibility and persistence.

The TOML parser, environment path auditing and control executable are implemented
and compiled, but browser startup still does not consume configuration. Early
startup wiring, native ProcessSingleton isolation checks and GURL validation
remain Phase 4 work. Preserve original argc/argv backing for Linux process titles.

Capability preservation (incognito persistence, extensions, broader DevTools,
downloads/history/bookmarks/passwords/media/restoration), release-mode build,
Arch package and maintenance rehearsal remain pending. No daily-driver alpha
claim has been made.

Focused browser-test compilation passed in 34m57s (2,925 actions), receipt
`.build/logs/product-test-build-20260908T191448.591365Z.json`. Matching no-op
receipt: `.build/logs/product-test-build-20260908T195158.091571Z.json`.
The focused browser test passed on actual X11 in 12 seconds:
`.build/test-evidence/product-browser-tests-20260908T195515.267037Z/results.json`,
build receipt `.build/logs/product-test-build-20260908T195439.628520Z.json`.
It checks native tab creation, activation, reordering, navigation and closing.
The first attempt failed before assertions because TMPDIR exceeded Linux's Unix
socket limit. Its log and scoped cleanup record remain at
`.build/test-evidence/product-browser-tests-20260908T195234.345213Z/`.
The corrected runner uses a short 0700 TMPDIR and checks socket path capacity;
five runner regression tests pass. This harness result supplements the actual
production launch gate; it does not establish the remaining UI capabilities.

Next integrate the reviewed local NTP and six browser tests, then:

```sh
python3 mb/tools/product.py prepare
python3 mb/tools/product.py gen
python3 mb/tools/product.py test-build --targets mb:mb_browser_tests --jobs 12
python3 mb/tools/browser_tests.py --build-receipt .build/logs/<successful-test-build>.json
python3 mb/tools/product.py build --jobs 12
```

## Completed gates and maintenance notes

Unmodified Chromium 152.0.7977.82, commit
`d04cdb24d67b081f6cf80200ffc5233f44b61109`, built successfully before any source
integration. Final baseline run: 6h45m16s, 26,086 completed actions. Receipt:
`.build/logs/upstream-build-20260908T112027.558220Z.json`; reviewed Xorg evidence:
`.build/test-evidence/upstream-20260908T181043.265925Z/review.json`.
See [upstream review](docs/upstream-baseline-2026-09-08.md).

Product integration checks pinned revisions and all 165 nested Git dependencies,
refuses unrelated edits, stages generated branding, and records hashes. Native
strings alias Chromium's GN-resolved resource-ID map; all locale repacks passed.
Real generated credits include toml++. Retiring a patch restores pinned source;
retiring owned generated files archives them. Manifest-only regeneration is tested.

Known upstream issue: typing percent-encoded data HTML triggered a debug omnibox
DCHECK; base64 fixtures pass. Initial product smoke had a post-DevTools navigation
timeout; two diagnostic retries passed, without a proven timing root cause.
F12 and Ctrl+T remain unverified. The latter awaits the local-only NTP.

Scoped toml++ unsafe-buffer diagnostics are suppressed only around its vendor
include; parser limits and runtime hardening remain. No sandbox, certificate,
same-origin or site-isolation protection has been disabled.

Scratch drafts awaiting sequential staging: local NTP at
`.build/tmp/ntp-work-z97_2drb`, expanded tests at
`.build/tmp/ntp-protection-work-20260908T`, window adapter at
`.build/tmp/window-layer-work-20260908T`, product header at
`.build/tmp/sidebar-work-swh82m0q`, and vertical policy at
`.build/tmp/sidebar-policy-5pguzvip`. Each requires compilation and runtime review.

# Implementation status

Updated: 2026-09-08.

- Current phase: Phase 3, native custom UI. Phases 1–2 passed. The first sidebar
  browser/Xorg input checkpoint passed; Phase 3 is not complete.
- Last successful browser build:
  `.build/logs/product-build-20260908T205919.455231Z.json`.
- Last successful browser launch/test: sandboxed Xorg production smoke, normal
  shutdown exit 0; `.build/test-evidence/product-20260908T211150.370203Z/review.json`.
- Last focused tests: 11 browser tests passed in 129s before the two small sidebar
  follow-ups; each follow-up passed all three sidebar tests. Latest:
  `.build/test-evidence/product-browser-tests-20260908T205809.598441Z/results.json`.
  Its build: `.build/logs/product-test-build-20260908T205630.672359Z.json`.
- Product core: 51 Chromium-built C++ tests and 17 companion end-to-end checks
  passed at `.build/test-evidence/product-unit-20260908T184731.229729Z/`.
  Latest tool suite: 18 integration/GRIT/runner checks passed at
  `standalone-20260908T200445.214481Z/results.json`. Earlier full standalone: 107
  passed at `standalone-20260908T100704.408098Z/results.json`.
- Blockers: none currently; dependencies installed and verified. No sudo run.
- Resources: about 81 GiB free disk, 10 GiB available RAM and 24 GiB unused swap
  during the last UI build. The below-100-GiB warning was reported. Recheck before
  a separate release output or large test build.
- Remote: `git@github.com:mattmezza/mb.git`, branch `main`. `.build/` is ignored.
  Unrelated root `test/pages/README.md` and `.tmux-session` remain untouched.

## Current checkpoint and next work

The initial native sidebar checkpoint passed, including basic Xorg tab inputs,
pinning/reorder/groups, resize/collapse and F12. See the dated sidebar review.
Four broader DevTools browser cases and 100-tab model correctness now pass.
The scale result does not establish responsiveness: loaded selection plus
idle/layout wait measured 298–524 ms. Latest evidence:
`.build/test-evidence/product-browser-tests-20260908T213237.269243Z/summary.json`.
Latest test build: `.build/logs/product-test-build-20260908T213104.109228Z.json`.
Latest production build: `.build/logs/product-build-20260908T213916.731086Z.json`.

Production 100-local-page startup failed loading/performance review. After a
network-service restart/rebind log, most tabs remained Loading for minutes;
reloading first/last pages worked. Native close-window exited 0. Evidence:
`.build/test-evidence/sidebar-input-20260908T214054.271075Z/review.json`.
An earlier 20-second scratch startup timeout and scoped cleanup failure are
retained at `sidebar-input-20260908T213945.201798Z`. No network/security bypass.
Cause is unresolved; add native operation/idle/layout timing decomposition and
child-process lifecycle diagnostics. No browser or build currently runs.

Next: compile/run keyboard/AX and loading/crash/audio tests, then the decomposed
scale measurement. Review AI/customization removal and generated NTP favicon.
Exact commands after each reviewed source integration:

```sh
python3 mb/tools/product.py prepare
python3 mb/tools/product.py gen
python3 mb/tools/product.py test-build --targets mb:mb_browser_tests --jobs 12
python3 mb/tools/browser_tests.py --build-receipt .build/logs/<successful-test-build>.json --filter 'MbSidebarAccessibilityBrowserTest.*:MbSidebarStateBrowserTest.*'
```

Do not run builds while product browser processes/tests use shared libraries.
Do not launch the old `out/mb-debug/chrome`: resources/shared libraries now carry
product integration.

## Later phases

The TOML parser, environment path auditing and control executable are compiled
and tested, but browser startup does not consume configuration yet. Phase 4
must wire raw argv normalization before CommandLine initialization, explicit
configuration/root selection before Profile/ProcessSingleton setup, then XDG
lifecycle, UI settings, environment activation and actual isolation tests.
Preserve original argc/argv backing for Linux process titles and native locks.

Full extension/DevTools/incognito persistence, ordinary capabilities, release
mode, Arch package installation and upstream-update rehearsal remain pending.
No daily-driver alpha claim has been made. Security protections remain enabled.

## Prepared scratch drafts (uncompiled unless stated)

- 100-tab measurement: `.build/tmp/sidebar-scale-20260908T`.
- DevTools: `.build/tmp/devtools-tests-20260908T`.
- AI/customization UI: `.build/tmp/omnibox-ui-20260908T-a`.
- Local NTP favicon: `.build/tmp/ntp-favicon-bVvuxn`.
- Raw startup arguments: `.build/tmp/startup-argv-6ghiymnt`.
- Explicit configuration gate: `.build/tmp/browser-config-gate-814cb32j`.
- Config error harness: `.build/tmp/config-gate-runner-w4oioC` (four pure Python
  harness tests pass; no browser execution).
- MV3 capability tests/fixture hooks: `.build/tmp/extensions-capability-20260908T`.
- Incognito cookie/history tests: `.build/tmp/incognito-tests-voabnycf`.
- Sidebar accessibility/keyboard draft: `.build/tmp/sidebar-accessibility-ZJrDi1`
  (requires correction/review of native vertical tab type before integration).

Scratch recipes/helpers are review aids, not accepted browser implementations.
Use successful build receipts matching the exact current staged integration.

## Earlier gates

Unmodified Chromium 152.0.7977.82, commit
`d04cdb24d67b081f6cf80200ffc5233f44b61109`, built and passed sandboxed Xorg review
before product source integration. Baseline build:
`.build/logs/upstream-build-20260908T112027.558220Z.json` (final 12-job run 6h45m16s).
See [upstream review](docs/upstream-baseline-2026-09-08.md),
[branding review](docs/product-baseline-2026-09-08.md), and
[local NTP implementation](docs/local-new-tab.md).

Integration verifies pinned revisions and 165 nested Git dependencies, refuses
unrelated edits, stages manifest-derived resources with unchanged native GRIT
IDs, and records build/test hashes. Real credits include toml++. No sandbox,
certificate, same-origin or site-isolation protection has been disabled.

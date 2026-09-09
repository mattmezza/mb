# Implementation status

Updated: 2026-09-09 (Europe/Zurich; evidence timestamps use UTC).

- Current phase: Phase 3. Unmodified upstream, branding and the first native
  sidebar checkpoints passed. Daily-driver acceptance is incomplete.
- Last successful production build:
  `.build/logs/product-build-20260908T221802.370989Z.json`.
- Last successful focused test build:
  `.build/logs/product-test-build-20260908T221422.316201Z.json`.
- Last focused run: cleanup/navigation and expanded scale trace, 3/3 passed at
  `product-browser-tests-20260908T221539.543092Z`.
- AX/Return focus passed at `215551.366276Z`; loading/crash/audio passed at
  `215044.420082Z`. Bulk navigation after readiness passed all 100 local pages
  at `221016.242372Z`, with no observed network-service lifecycle events.
- Product core: 51 native C++ tests and 17 companion checks passed at
  `product-unit-20260908T184731.229729Z`. Latest focused tool suite: 18 passed
  at `standalone-20260908T200445.214481Z`; earlier full standalone suite: 107.
- Full product sandbox smoke last passed at `product-20260908T211150.370203Z`.
  Current cleanup build is undergoing additional actual Xorg review below.
- Resources: about 78 GiB disk free, 17 GiB available RAM and 24 GiB unused swap
  after the last bulk review. Below-100-GiB warning already reported. Recheck
  before a separate optimized output; release peak storage remains unmeasured.
- External blockers: none currently. Dependencies verified; no sudo run.
- Git: main, `git@github.com:mattmezza/mb.git`; last pushed `cf73541`.
  Unrelated root `test/` and `.tmux-session` remain untouched.

## Current work and exact continuation

The previous bounded review ended during the limits pause; retained evidence
`sidebar-input-20260908T221951.174452Z` includes its forced cleanup.
A fresh review at `sidebar-input-20260909T072331.107633Z` painted the local NTP
without the inherited AI chip. Desktop focus left the owned window repeatedly;
keyboard navigation timed out and the helper cleaned up its own process group.
This is not a keyboard pass or a clean shutdown pass. No review browser remains.

Matching NTP/sidebar regression passed 9/9 at
`product-browser-tests-20260909T072551.791965Z` (suite names checked in source):

```sh
python3 mb/tools/browser_tests.py --build-receipt .build/logs/product-test-build-20260908T221422.316201Z.json --filter 'MbNewTabBrowserTest.*:MbSidebarBrowserTest.*'
```

Cleanup patches 0005/0006 compile and pass focused browser tests. Additional
production private-window/input review remains pending.

## Open performance issue

100-tab model/view/presentation checks pass, but debug responsiveness does not.
Loaded native activation takes about 240–290 ms. Native trace attributes a
substantial part to BrowserView's full layout; no layout or security bypass has
been added. The initial production launch with 100 local URL arguments logged
a network-service restart and left most pages loading for minutes. Reloads
worked, and normal shutdown exited 0. Evidence:
`sidebar-input-20260908T214054.271075Z/review.json`.

The after-readiness diagnostic loaded all 100 local pages in 53 seconds without
a service exit. This narrows startup conditions but does not resolve the failure.
A test-only initial-startup variation is ready for review/compilation at
`.build/tmp/bulk-startup-data-irxmi29j`. Release performance remains untested.

## Next phases

Phase 4 must wire the tested TOML/runtime/path core into browser startup, add
XDG defaults and UI settings, then prove separate environment roots, native
process activation/locking and cookie/history/extension isolation. Preserve
original argc/argv backing and Chromium's genuine off-the-record profiles.
Prepared drafts: `.build/tmp/startup-argv-6ghiymnt`,
`.build/tmp/browser-config-gate-814cb32j`, and
`.build/tmp/config-gate-runner-w4oioC` (four pure harness tests passed).

Broader MV3/incognito/DevTools panels and ordinary capabilities, optimized build,
Arch packaging/installation and upstream-update rehearsal remain open.
Prepared capability drafts: `.build/tmp/extensions-capability-20260908T` and
`.build/tmp/incognito-tests-voabnycf`. Closed debug/release tooling proposal:
`.build/tmp/release-profiles-20260909T-a` (11 scratch pure tests passed).
These drafts are not accepted browser implementations until reviewed and tested.

See docs/testing.md and the dated upstream/branding/sidebar reviews for exact
successes, failures and scope. Do not launch old `out/mb-debug/chrome`: its
shared libraries and resources now carry product integration.

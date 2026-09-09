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
- Git: main, `git@github.com:mattmezza/mb.git`; last pushed `def5355`.
  Unrelated root `test/` and `.tmux-session` remain untouched.

## Desktop availability

The user is using this computer for morning meetings. Keep work lightweight:
no browser launches, desktop input, or heavy builds before 11:35 Europe/Zurich.
The user explicitly reserved **2026-09-09 11:35–14:00 Europe/Zurich**
(09:35–12:00 UTC) for our unrestricted builds and desktop testing. Use that
window for the initial-startup diagnostic, incremental configuration checks,
and pending Xorg/private-window review. Stop interactive testing and close
owned test windows before 14:00. A full release build needs a separate longer
slot; do not start it on the assumption it will finish within lunch.

No browser or build is currently running. The 181-file staged checkout predates
new root tooling changes; run prepare again before future generation.

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
The reviewed initial-startup variation is now in
`mb/test/bulk_startup_data_browsertest.cc`, pending compilation and execution.
It registers native network lifecycle observation after threads are created,
before Mojo/profile startup, preserving early-exit observer cleanup.
Release performance remains untested.

Queued build for the next available desktop/build window (not running):

```sh
python3 mb/tools/product.py prepare
python3 mb/tools/product.py gen
python3 mb/tools/product.py test-build --targets mb:mb_browser_tests
```

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
`.build/tmp/incognito-tests-voabnycf`. Debug/release tooling is integrated in root; 27 profile/integration/runner/smoke
pure tests pass. Recorded integration-tools run 073010.941754Z passed 29;
upstream-tools found one Python 3.12 deep-JSON rejection failure. Explicit
parser bounds fixed it; 24 upstream-tool tests passed at 073416.970098Z.
Profile/process guard suite passed 37 tests at 073714.792255Z.
No release generation or compilation has run.
These drafts are not accepted browser implementations until reviewed and tested.

See docs/testing.md and the dated upstream/branding/sidebar reviews for exact
successes, failures and scope. Do not launch old `out/mb-debug/chrome`: its
shared libraries and resources now carry product integration.

## Prepared lunch sequence

1. Check the 11:35–14:00 local availability window and current RAM/disk; run the
   live-output guard. Preserve any user-owned trial process if still present.
2. Prepare/generate/build the initial-startup diagnostic, then run only
   `MbBulkStartupDataBrowserTest.*` with the matching new test receipt.
3. Integrate raw argv 0007 as one small milestone; compile/run its six unit
   cases and actual executable startup checks before proceeding.
4. Integrate explicit config gate 0008 plus fail-closed native-root fallback
   0009, then compile/run scoped unit and expected-error subprocess checks.
5. Use the remaining window for actual Xorg/private-window review and valid
   two-root native process launch/activation checks. Do not claim final
   environment isolation without cookie/history/extension evidence.

Scratch review locations:
- Raw argv: `.build/tmp/startup-argv-6ghiymnt` (0007, six cases, not compiled).
- Explicit gate: `.build/tmp/browser-config-gate-814cb32j` (0008, eight cases,
  not compiled).
- Fallback: `.build/tmp/native-root-fail-closed-seoz1fc8` (0009, before native
  default-directory resolution; deterministic test under preparation).
- Expected-error runner: `.build/tmp/config-gate-runner-w4oioC` (five pure tests
  passed, including release receipt support; real executable checks pending).
- Release payload source inventory: `.build/tmp/release-payload-inventory-20260909`.

Hands-on user instructions are in `docs/experimental-testing.md`. No browser or
heavy compilation is being launched during the morning meeting period.

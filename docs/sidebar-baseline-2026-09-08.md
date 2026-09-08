# First native sidebar checkpoint — 2026-09-08

The product sidebar builds and passes its first browser and actual Xorg input
checks. Phase 3 remains open for 100-tab responsiveness, broader keyboard and
accessibility coverage, status indicators, and UI cleanup. This is not a
complete daily-driver alpha.

## Implementation

`mb::SidebarView` composes a manifest header, live tab count and browser-command
menu inside Chromium's native vertical region. Its one native tab projection
continues to use TabStripModel. BrowserWindowAdapter resolves live native tab
handles and routes commands to Chromium. Normal Linux browser windows are fixed
to vertical orientation through the controller, commands, menus and Settings.
Only horizontal-to-vertical onboarding is suppressed; native collapse and hover
controls remain available. Patch 0004 touches 18 upstream files.

## Build and browser tests

- Full initial suite: 11 tests passed in 129 seconds,
  `.build/test-evidence/product-browser-tests-20260908T204646.141256Z/results.json`.
  It covers the tab baseline, six local-NTP protections, live adapter handles,
  one sidebar projection, orientation policy and cross-window width/collapse.
- Onboarding follow-up: three sidebar tests passed in 38 seconds,
  `product-browser-tests-20260908T205207.304275Z/results.json`.
- Header geometry correction: three sidebar tests passed in 37 seconds,
  `product-browser-tests-20260908T205809.598441Z/results.json`.
  Test build: `.build/logs/product-test-build-20260908T205630.672359Z.json`.
- Current production build:
  `.build/logs/product-build-20260908T205919.455231Z.json`.
  Binary SHA-256:
  `625ea3655a714e419075d5d29ee59937025c2b964304b3370eafa6dd04e6fac1`.

GN header dependency checks pass. The machine remains the recorded Arch/Xorg
x86-64 laptop; this is a debug component build on the real `DISPLAY=:0` session.

## Reviewed production behavior

The final production smoke passed navigation and tab shortcuts, native DevTools,
manifest process/window identity, renderer sandbox checks and normal UI shutdown
with exit code 0. Review:
`.build/test-evidence/product-20260908T211150.370203Z/review.json`.
The visible horizontal strip is absent; the compact header and native tabs sit
at the top of the sidebar. All 12 sampled renderers had Seccomp filters and
no-new-privileges. The native sandbox page reports namespace, PID/network,
Seccomp-BPF and TSYNC protection.

Two separately reviewed input sessions use the same binary and fresh private
roots. Their screenshots demonstrate:

- Product menu and Ctrl+T tab creation, rendered local NTP, titles and selection.
- Context-menu pinning of Beta and selection of its page.
- Drag reordering of unpinned Alpha/Gamma into Gamma/Alpha, retaining the pinned
  section; clicking reordered Alpha renders its matching page.
- Active-tab close and product-menu reopen through native commands.
- Sidebar collapse to 56 pixels, expansion, and mouse resize from about 240 to
  322 pixels. The private test profile saved width 322 and collapsed false.
- F12 opening real DevTools Elements on Gamma, and Ctrl+Shift+I closing it.
- Creating and naming a native Review group, collapsing and expanding it, and
  selecting its Alpha child after expansion.

Input reviews:
`sidebar-input-20260908T205955.929741Z/review.json` and
`sidebar-input-20260908T210858.550920Z/review.json` under `.build/test-evidence/`.
Those sessions ended in harness cleanup, so they supply interaction evidence
only. The separate final production smoke supplies clean-shutdown evidence.

## Failures retained and corrected

Initial compilation found a Lit reactive-property declaration mismatch and the
pinned prohibition on ScopedObservation for TabStripModel. The product now uses
the native observer's lifetime handling, with lint and compiler checks enabled.

The first production review failed despite passing initial tests: a header text
FlexSpecification permitted growth in both axes, pushing the tabs halfway down
the window. Horizontal-only flex fixes it; a browser geometry assertion now
checks that the header reserves its content height. Failed review:
`product-20260908T205431.087004Z/review.json`.

The scratch input harness had two independent errors: synchronized movement to
the pointer's existing coordinates timed out, and Ctrl+Shift+Q was not the Linux
window-close binding. Both caused scoped cleanup of only the test browser.
The helper now avoids synchronized same-position movement and uses
Ctrl+Shift+W. No browser security or normal command behavior changed for these
harness fixes.

Several early captures preceded painting. They are not rendering evidence;
settled captures are explicitly identified in the review records. In particular,
the final smoke's NTP image is pre-paint; the input session's settled local NTP
image supplies that check against the identical binary.

## Pending scope

100-tab timing and real responsiveness, complete keyboard/focus and screen-reader
coverage, scrolling at scale, loading/crash/audio/mute states and private-window
visual review remain pending. Native implementations remain present but are not
accepted merely because they are inherited.

Inherited AI Mode, sign-in and unsupported Customize Chrome affordances remain
cleanup. The local NTP favicon still uses Chromium artwork; a manifest-derived
replacement is prepared. The version page's revision is zero-filled; the exact
pinned Git commit is independently verified in build receipts. Startup config,
environment isolation, full capabilities, release and Arch packaging follow.

## Reproduction

```sh
python3 mb/tools/product.py prepare
python3 mb/tools/product.py gen
python3 mb/tools/product.py test-build --targets mb:mb_browser_tests --jobs 12
python3 mb/tools/browser_tests.py --build-receipt .build/logs/<successful-test-build>.json
python3 mb/tools/product.py build --jobs 12
python3 mb/tools/product_smoke.py --build-receipt .build/logs/<successful-product-build>.json --local-ntp
```

Use fresh test roots. Review screenshots after they settle; launch automation
alone does not establish the full sidebar or alpha acceptance criteria.

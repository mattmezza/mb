# Known limitations

Updated 2026-09-09. Phase 1 passed: unmodified Chromium built and launched
under actual Xorg with reviewed navigation, tabs, docked DevTools, sandbox and
clean exit. A product debug build and its focused GN tests also passed. The
first product smoke had a post-DevTools navigation timeout; two retries passed.
The [reviewed product launch](product-baseline-2026-09-08.md) verifies its limited
scope. This repository does not yet provide the requested daily-driver browser.
The initial custom sidebar is running; its remaining performance/input checks,
runtime environment isolation, full extension compatibility, release build and
Arch packaging remain pending. See the [baseline review](upstream-baseline-2026-09-08.md)
for the upstream data-URL debug assertion. F12 passed the first sidebar Xorg
input check; see [sidebar review](sidebar-baseline-2026-09-08.md). Ctrl+T now
passes the [local NTP gate](local-new-tab.md). Inherited omnibox AI Mode hints
and unsupported new-tab customization controls are now omitted on Linux;
focused cleanup/navigation and nine NTP/sidebar regression cases pass. Broader
Google-dependent background service behavior remains inherited and unverified.

The compiled configuration companion works independently. Its parser and
environment-directory core have focused tests, but browser startup does not
yet consume them. Directory validation and secure creation do not establish
cookie, history, extension, or process-lock isolation; those require the
planned Chromium integration and browser tests.

Branding generators produce native inputs and an original temporary icon.
These resources are linked into the successfully tested branded browser. Changed translated messages
fall back to English, so the initial product will contain mixed-language UI
until product translations are available. The temporary wordmark slots use
the original icon without lettering.

The pinned unbranded upstream arguments retain Chromium's codec defaults:
`proprietary_codecs=false` and `ffmpeg_branding="Chromium"`. Proprietary codec
and DRM playback support is not promised. No unofficial Google API keys are
included. Google-dependent services and Chrome Web Store installation remain
unverified; the focused unpacked MV3 NTP override/disable test passes, while the broader
extension capability suite remains pending.

Only Arch Linux, x86-64, under Xorg is the initial target. Wayland, Windows,
macOS, automatic updates, accounts, and cloud synchronization are outside the
first release scope. Release updates will use package replacement.

Daily-driver acceptance remains incomplete. Standalone test
success is recorded separately from browser capability evidence in
[testing](testing.md).

The 100-tab debug correctness case passes, but responsiveness does not yet pass:
loaded selection plus the test's idle/layout wait measured 298–524 ms, and bulk
startup of 100 local pages is visibly slow. See [measured scope](testing.md).
Four native DevTools browser tests pass; full panel and extension-debugging
coverage remains pending.

Production debug bulk startup with 100 local data pages also failed its loading
review: a network-service restart/rebind was logged and most pages stayed
Loading for minutes; individually reloaded first/last pages rendered. Cause is
unresolved. Evidence: `sidebar-input-20260908T214054.271075Z/review.json`.
The same run closed normally with exit 0. No network or sandbox changes were
made to bypass it.

The after-readiness bulk diagnostic loaded all 100 local pages without an
observed network-service exit. It does not reproduce initial process startup;
a separate initial-argument diagnostic is prepared but not yet compiled.
Debug/release tooling is implemented and unit-tested, but no optimized build
or installed Arch package has passed. Experimental use should keep a separate
test user-data directory and close all windows before rebuilding shared output.

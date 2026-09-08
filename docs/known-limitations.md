# Known limitations

Updated 2026-09-08. Phase 1 passed: unmodified Chromium built and launched
under actual Xorg with reviewed navigation, tabs, docked DevTools, sandbox and
clean exit. A product debug build and its focused GN tests also passed. The
first product smoke had a post-DevTools navigation timeout; two retries passed.
The [reviewed product launch](product-baseline-2026-09-08.md) verifies its limited
scope. This repository does not yet provide the requested daily-driver browser.
Custom UI, runtime isolation, extension compatibility, release build and Arch
packaging remain pending. See the [baseline review](upstream-baseline-2026-09-08.md)
for the upstream data-URL debug assertion and unverified F12 check. Ctrl+T now
passes the [local NTP gate](local-new-tab.md); the inherited omnibox AI Mode
affordance and irrelevant Customize Chrome controls remain UI cleanup.

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

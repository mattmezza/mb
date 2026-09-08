# Known limitations

Updated 2026-09-08. Phase 1 passed: unmodified Chromium built and launched
under actual Xorg with reviewed navigation, tabs, docked DevTools, sandbox and
clean exit. This repository is entering Phase 2 and does not yet provide a
usable product browser. Custom UI, runtime isolation, extension compatibility,
release build and Arch packaging remain pending. See the [baseline review](upstream-baseline-2026-09-08.md)
for the upstream data-URL debug assertion and unverified F12/Ctrl+T checks.

The compiled configuration companion works independently. Its parser and
environment-directory core have focused tests, but browser startup does not
yet consume them. Directory validation and secure creation do not establish
cookie, history, extension, or process-lock isolation; those require the
planned Chromium integration and browser tests.

Branding generators produce native inputs and an original temporary icon.
These resources are not yet linked into Chromium. Changed translated messages
fall back to English, so the initial product will contain mixed-language UI
until product translations are available. The temporary wordmark slots use
the original icon without lettering.

The pinned unbranded upstream arguments retain Chromium's codec defaults:
`proprietary_codecs=false` and `ffmpeg_branding="Chromium"`. Proprietary codec
and DRM playback support is not promised. No unofficial Google API keys are
included. Google-dependent services and Chrome Web Store installation remain
unverified; unpacked extension support also awaits runtime testing.

Only Arch Linux, x86-64, under Xorg is the initial target. Wayland, Windows,
macOS, automatic updates, accounts, and cloud synchronization are outside the
first release scope. Release updates will use package replacement.

All twenty daily-driver acceptance criteria remain open. Standalone test
success is recorded separately from browser capability evidence in
[testing](testing.md).

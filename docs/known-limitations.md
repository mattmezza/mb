# Known limitations

Updated 2026-09-08. This repository is in Phase 1 and does not yet provide a
usable browser. The unmodified pinned Chromium build is running. No browser
launch, custom UI, runtime isolation, extension compatibility, release build,
or Arch package has passed acceptance testing. See [STATUS](../STATUS.md) for
the active command and evidence.

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

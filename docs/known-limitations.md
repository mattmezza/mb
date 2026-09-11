# Known limitations

Updated 2026-09-11. The optimized browser and Arch package now build, and the
release binary has passed explicit two-environment and default-XDG lifecycle
smoke tests under Xorg. Daily-driver alpha acceptance remains incomplete until
the installed package and the remaining physical desktop checks pass.

## Distribution and platform

- Arch Linux x86-64 under Xorg is the only tested target. Wayland, Windows,
  macOS and mobile platforms are outside the first release.
- The Arch archive is built and inspected, including root ownership metadata and
  mode 4755 for `chrome-sandbox`. It has not yet been installed system-wide, so
  the installed setuid fallback has not been exercised.
- Updates use package replacement. There is no automatic updater.
- The active X11 window manager has repeatedly defeated synthetic focus and
  close events. Clipboard, IME, file picker, HiDPI and multi-monitor behavior
  therefore require a short physical-input pass.

## Browser services

- Unpacked Manifest V3 installation, enable/disable/removal, content scripts,
  action popups, background workers and extension storage pass focused tests.
  Chrome Web Store installation and complete extension compatibility are not
  claimed. No unofficial Google API keys are included.
- Chromium DevTools commands, page-context inspection, normal/private windows,
  and docked/undocked creation pass focused tests. Every DevTools panel has not
  been exhaustively tested.
- The unbranded build retains Chromium codec defaults
  (`proprietary_codecs=false`, `ffmpeg_branding="Chromium"`). Proprietary media
  codecs and DRM playback are not promised.
- Availability of Safe Browsing and other Google-backed services follows
  unbranded Chromium. No service restriction is bypassed.
- Translated product-specific strings fall back to English until translations
  are supplied.

## Configuration and environments

- `personal` and `work` roots pass cross-restart isolation checks for cookies,
  history, installed extensions and extension storage. Chromium's normal
  user-data-root locking remains authoritative.
- Genuine off-the-record profiles pass cookie and normal-history persistence
  checks. Extension incognito access still follows Chromium's native opt-in
  controls; the permission UI needs final manual verification.
- Remembered environment selection is stored only for the default XDG
  configuration. An explicitly supplied `--config` remains stateless.
- Synchronous initial session restoration receives configured sidebar state.
  Additional profiles and asynchronous `kNo` restore paths are not forcefully
  overwritten after the initial restore completes.

## UI and performance

- The sidebar uses Chromium's `TabStripModel` and native vertical-tab views.
  Correctness tests cover selection, close, reorder, pins, groups, scrolling,
  state indicators, accessibility, collapse and resize behavior.
- The 100-tab debug activation measurements are slow and are not representative
  of optimized performance. Optimized whole-test cases complete in 14–28s, but
  no per-interaction latency benchmark or long-lived memory study exists yet.
- The omnibox and navigation controls intentionally retain Chromium's current
  layout while vertical tabs stabilize.

See [testing](testing.md) for exact evidence and [security](security.md) for
preserved Chromium security boundaries.

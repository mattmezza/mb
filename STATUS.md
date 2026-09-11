# Implementation status

Updated 2026-09-11, Europe/Zurich. Current phase: 6; daily-driver alpha pending
physical desktop verification.

## Verified

- Chromium 152.0.7977.82 is pinned at `d04cdb24d67b081f6cf80200ffc5233f44b61109`.
- The optimized non-component build completed in 11h58m:
  `.build/logs/product-build-20260910T104247.448126Z.json`.
- Release binary SHA-256:
  `7c1e2d8b074d037066ad770be5e1eb9c2ea9bd1fb68516f3f037e93bde2645f3`.
- Release-mode 73 native tests and 17 companion checks passed:
  `.build/test-evidence/product-unit-20260911T125917.640304Z`.
- All 39 optimized browser-test executions passed:
  `.build/test-evidence/product-browser-tests-20260911T125932.943720Z`.
- Optimized 100-tab cases completed in 14.2s, 19.1s and 28.1s.
- Release environment/XDG smokes passed in `two-env-ynp0j2fm` and
  `default-config-_efwovxy`.
- Arch package staging tests pass. The 248 MiB archive contains 257 payload
  files, root-owned metadata, sandbox mode 4755, all locales, generated credits,
  desktop metadata and icons:
  `.build/packages/mb-browser-0.1.0-1-x86_64.pkg.tar.zst`.
- Package SHA-256:
  `3fb8cd96d5ad74b0abed297032019a602cb28d0e186e691f30e32bd26ea2e6c7`.
- Pacman reports 288 installed files with none missing. `/opt/mb/mb` matches the
  tested release hash and `/opt/mb/chrome-sandbox` is root:root mode 4755.
- The installed `/usr/bin/mb` opened under X11 and exited cleanly. Renderer,
  network and storage children had `NoNewPrivs=1` and Seccomp mode 2:
  `.build/test-evidence/installed-package-J1JYhtGf`.
- All standalone product/tooling suites passed in
  `.build/test-evidence/standalone-20260911T130750.905118Z`.
- The read-only update check verified Chromium 153.0.8010.36 as a newer Linux
  Stable candidate without changing the pinned alpha baseline.

## Pending

1. Complete physical checks for clipboard, IME, file picker, downloads,
   notifications, media, HiDPI/multiple monitors and default-browser invocation.
2. Manually check bookmarks, permissions, passwords, session restoration,
   extension incognito permission UI and the main DevTools panels.
3. Record the physical results and finish the acceptance report.

## Limits

- Chrome Web Store and complete extension compatibility are not claimed.
- Proprietary codecs/DRM are unavailable with the retained Chromium codec build.
- Explicit `--config` selection is stateless; remembered selection uses the
  default XDG configuration.
- Unrelated root `test/` and `.tmux-session` remain untouched.

## Continue

Run `mb --environment personal` for the physical desktop checklist. The
remaining automated work is documentation and upstream-update validation.

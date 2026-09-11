# Browser fork workspace

This repository contains a direct Chromium fork for Linux/X11. Debug and
optimized product builds, focused native tests, isolated-environment Xorg
launches, and an Arch package build have passed.
The product's temporary identity is centralized in [one
branding manifest](mb/branding.toml); its tested generator prepares build inputs
separately from the unmodified Chromium checkout.

Start with [STATUS.md](STATUS.md) and the
[implementation plan](docs/implementation-plan.md). The Chromium source and
depot_tools revisions are pinned in [upstream-version.toml](mb/upstream-version.toml).

```sh
python3 mb/tools/bootstrap.py doctor
```

See [Arch build setup](docs/build-arch-linux.md) for dependency installation and
fetching. Local downloads and build outputs live under ignored `.build/`.
The existing `.tmux-session` file is user-owned and has not been changed.

The pinned, unmodified source built and passed a sandboxed launch on the actual
Xorg session; see the [baseline review](docs/upstream-baseline-2026-09-08.md).
Product branding integration has produced successful debug and release builds,
focused test receipts and a [reviewed product launch](docs/product-baseline-2026-09-08.md).
The native-sidebar Xorg checkpoint passed basic tab inputs,
pinning/reorder/groups, resize/collapse and F12. Focused DevTools routes and
100-tab model correctness passed. Optimized 100-tab browser tests complete in
about 14–28 seconds. AX name/selection, Return activation and
focus, plus loading/crash/audio checks passed in focused product runs. Expanded
scale correctness and next-frame checks passed without establishing
responsiveness. Broader AT-SPI/screen-reader, pointer/mute, browser
configuration wiring and environment isolation are tested. Physical X11 checks
and an installed-package smoke remain before daily-driver acceptance.
The generated Arch package includes Chromium's license and generated third-party
credits.

The standalone control command is available:

```sh
python3 mb/tools/build_control.py
.build/control/mbctl --config mb/config/example.toml config validate
```

It validates TOML and audits environment paths without creating browser data.
The browser consumes the same schema at startup. See
[configuration](docs/configuration.md) and [environment paths](docs/environments.md).

After source bootstrap and hooks, run the standalone product checks with:

```sh
.build/depot_tools/python-bin/python3 mb/tools/test_product.py
```

Use `--list` to inspect commands or `--suite` to select focused checks. The
[test record](docs/testing.md) distinguishes these results from pending browser
runtime verification.

Use `python3 mb/tools/check_upstream.py` to report the official Linux stable
candidate without changing source or pins. The [update procedure](docs/upstream-updates.md)
records the maintenance gates and work still required to rehearse a full update.

Chromium's source license is retained verbatim in [LICENSE.chromium](LICENSE.chromium).
Vendored toml++ retains its MIT license and provenance. The tested package is
written beneath `.build/packages/` and includes generated third-party notices.

For hands-on checks of the release build, see
[experimental testing](docs/experimental-testing.md). Close trial windows before rebuilding.

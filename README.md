# Browser fork workspace

This repository is preparing a direct Chromium fork for Linux/X11. A product
browser build, focused GN tests and scoped sandboxed Xorg launch have passed.
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
Product branding integration has produced a successful debug build and focused
test receipts and a [reviewed product launch](docs/product-baseline-2026-09-08.md).
The initial native-sidebar Xorg checkpoint passed basic tab inputs,
pinning/reorder/groups, resize/collapse and F12. Focused DevTools routes and
100-tab model correctness passed; the 100-local-page startup/performance review
failed and its cause remains unresolved. AX name/selection, Return activation and
focus, plus loading/crash/audio checks passed in focused product runs. Expanded
scale correctness and next-frame checks passed without establishing
responsiveness. Broader AT-SPI/screen-reader, pointer/mute, browser
configuration wiring, and remaining native UI cleanup are still pending.
Release-mode output, package assembly and daily-driver acceptance remain open.
The full daily-driver acceptance remains pending. Required Chromium licenses,
notices and credits must accompany any eventual distribution; no distributable
package is available yet.

The standalone control command is available for development:

```sh
python3 mb/tools/build_control.py
.build/control/mbctl --config mb/config/example.toml config validate
```

It validates TOML and audits environment paths without creating browser data.
See [configuration](docs/configuration.md) and [environment paths](docs/environments.md)
for commands, tests, and current integration limits.

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
Vendored toml++ retains its MIT license and provenance. Binary packages must also
include generated third-party notices; no package is available yet.

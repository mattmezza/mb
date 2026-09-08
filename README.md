# Browser fork workspace

This repository is preparing a direct Chromium fork for Linux/X11. There is no
custom browser binary yet. The product's temporary identity will be defined by
one branding manifest after the unmodified upstream build passes.

Start with [STATUS.md](STATUS.md) and the
[implementation plan](docs/implementation-plan.md). The Chromium source and
depot_tools revisions are pinned in [upstream-version.toml](mb/upstream-version.toml).

```sh
python3 mb/tools/bootstrap.py doctor
```

See [Arch build setup](docs/build-arch-linux.md) for dependency installation and
fetching. Local downloads and build outputs live under ignored `.build/`.
The existing `.tmux-session` file is user-owned and has not been changed.

The source checkout must build and launch under Xorg with Chromium's sandbox
enabled before any product integration. All daily-driver acceptance criteria
remain pending. Required Chromium licenses, notices and credits must accompany
any eventual distribution; no distributable package is available yet.

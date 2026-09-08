# Discovery: 2026-09-08

This is an inspection record for the initial Arch Linux development machine. No Chromium build, launch, or product acceptance test has passed yet.

## Workspace and machine

| Item | Observed state |
| --- | --- |
| Workspace | `/home/matteo/dev/mb`; initially empty apart from `.tmux-session`, which is preserved |
| Project instructions | No applicable `AGENTS.md` found during initial inspection |
| Platform | Arch Linux, x86-64 |
| CPU | Intel Core Ultra 5 235U; 12 cores, 14 logical CPUs |
| Memory | About 30 GiB RAM, 14 GiB available at inspection |
| Swap | About 47 GiB total, 19 GiB used; includes 32 GiB disk swap and 15.4 GiB zram |
| Working disk | About 146 GiB available on the root filesystem |
| Temporary directory | `/tmp` is tmpfs with about 3.8 GiB available |
| Display | Xorg 21.1.24, accessible at `DISPLAY=:0` |
| Monitors | Internal 1920×1200 display and two external 1920×1080 displays |
| User namespaces | Enabled; `user.max_user_namespaces=126024` |
| Git | 2.55 |
| Python | User shim 3.12.12; system Python 3.14.7 |
| System compiler | GCC 16.2; no system Clang found |
| Chromium tools/source | No source checkout, GN, or `depot_tools` found |

The existing `.cache/chromium` directory is browser cache, not a Chromium source checkout. Do not reuse or remove it for the build.

The machine exceeds the requested approximate 100 GB free-space and 16 GB RAM thresholds. The available disk margin remains limited for Chromium sources, intermediate files, and two build configurations. Existing swap use also argues for conservative concurrency. Start at four build jobs, measure actual memory and disk use, and adjust only from those measurements. Recheck capacity before syncing and before each build. Keep build temporary files on the working disk rather than the small `/tmp` tmpfs.

## Dependencies and external gate

The pinned release's [Linux build instructions](https://chromium.googlesource.com/chromium/src/+/refs/tags/152.0.7977.82/docs/linux/build_instructions.md) include an Arch package list:

```text
python perl gcc gcc-libs bison flex gperf pkgconfig nss alsa-lib glib2 gtk3 nspr freetype2 cairo dbus xorg-server-xvfb xorg-xdpyinfo
```

Arch's `pkgconf` supplies the listed `pkgconfig` dependency. Initial package inspection found only `gperf` missing from this list. `pacman -Si gperf` confirmed official repository package `extra/gperf` version `3.3-2`. Chromium's build uses this perfect-hash generator for generated lookup tables.

The user must run:

```sh
sudo pacman -S --needed gperf
```

Then confirm completion. The agent must verify installation before resuming dependency-dependent work. The agent will not run `sudo`, Debian dependency installers, or remote scripts piped into a shell. Additional dependencies discovered by the pinned checkout will be documented individually before requesting installation.

Use Chromium's pinned downloaded compiler, GN, build tooling, and sysroot rather than assuming the system GCC or Python shim is the supported build toolchain. Downloaded tool revisions and bootstrap behavior still require verification against the chosen checkout.

## Upstream selection

The official [ChromiumDash releases service](https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Linux&num=1) reported Linux stable version `152.0.7977.82` with Chromium commit `d04cdb24d67b081f6cf80200ffc5233f44b61109` on the selection date, 2026-09-08. Gitiles verification confirmed that the corresponding tag resolves to that exact commit. The selected `depot_tools` revision is `5ac00be14bc55f60335d0b284bc7e0fd6b4c0e69`. The machine-readable pin in `mb/upstream-version.toml` is authoritative; this discovery document is a dated snapshot.

## Next verification

After the dependency gate, fetch the pinned source into `.build/chromium/src`, using `.build/depot_tools`. Build and launch unmodified Chromium under the actual Xorg session with its sandbox enabled. User-namespace availability alone does not prove that Chromium's sandbox works. No Chromium integration changes may precede the successful upstream build and launch gate.

The intended resume sequence, once the user confirms package installation, is:

```sh
python3 mb/tools/bootstrap.py doctor
python3 mb/tools/bootstrap.py fetch
```

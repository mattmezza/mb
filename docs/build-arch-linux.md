# Arch Linux build setup

Status: dependency preflight, pinned source/dependency fetch, hooks, and GN
generation pass. The first unmodified browser build is running.
Python 3.11+ is needed for the bootstrap tool's standard-library TOML reader.

## Dependencies

The [pinned Chromium instructions](https://chromium.googlesource.com/chromium/src/+/refs/tags/152.0.7977.82/docs/linux/build_instructions.md)
list Arch packages directly. The bootstrap tool checks these using `pacman -Q`,
mapping `pkgconfig` to Arch's `pkgconf`, and also checks Git.

```sh
python3 mb/tools/bootstrap.py doctor
```

The initial inspection found only `gperf` missing. It generates perfect-hash
lookup code used by the Chromium build. The user installed it with:

```sh
sudo pacman -S --needed gperf
```

Installation was verified as `gperf 3.3-2` on 2026-09-08. For a fresh machine,
run the doctor command and install its reported missing official Arch packages.
No Debian dependency installer or AUR package is needed. Chromium's pinned
hooks supply its compiler, GN and other tools; absent system Clang/GN packages
are not a reason to install substitute toolchains.

## Fetch

From the repository root, after the dependency gate passes:

```sh
python3 mb/tools/bootstrap.py fetch
```

This initializes `.build/depot_tools` and `.build/chromium/src` at the recorded
commits, verifies the stable tag, and runs `gclient sync --nohooks --no-history`
with the explicit Chromium revision. It never fetches tip-of-tree Chromium
first. depot_tools auto-update and metrics are disabled in the child process;
temporary files use `.build/tmp`. It refuses to replace mismatched repositories,
move an existing HEAD, or overwrite a different `.gclient`.

Because disabling depot_tools auto-update also skips its Python bootstrap, the
fetch tool explicitly invokes the pinned local `bootstrap_python3` function.
This downloads the manifest-pinned interpreter and creates the `python-bin`
entrypoints used by GN/autoninja without updating the depot_tools revision.

An interrupted source fetch can be retried. If Git reports a lock or the tool
detects local changes, inspect the failure before acting; do not blindly delete
locks, reset, or clean the checkout. Updates to a new revision require a separate
reviewed update workflow, not this initial bootstrap command.

The first fetch requires 100 GiB free. Each retry and dependency sync requires a
25 GiB reserve, because a partial checkout already occupies some of the original
budget. These checks are conservative stop points, not a prediction of total
download/build size or continuous monitoring. Recheck disk usage during long
downloads and before building; the tool warns whenever free space is below
100 GiB. Do not remove unrelated files to make room.

## Unmodified build gate

The pinned hooks and GN arguments have been inspected. After fetching finishes,
run each stage successfully before starting the next:

```sh
python3 mb/tools/upstream.py hooks
python3 mb/tools/upstream.py gen
python3 mb/tools/upstream.py build --jobs 4
```

Hooks and GN generation have passed on this machine; the build is running. Each
stage verifies both Git pins plus nested Git dependency revisions and requires
clean tracked/untracked source, then
writes a timestamped log and result receipt under `.build/logs`. Do not run
hooks concurrently with fetching. The stage tool refuses a different existing
`args.gn` and a build without generated Ninja files.

Hooks use the downloaded upstream toolchain/sysroot. This is a sysroot download,
not execution of Debian's system dependency installer on Arch. The baseline
arguments are checked in at `mb/tools/gn/upstream-debug.gn`: debug component
build, symbol level 1, Blink/V8 symbols 0, local Siso, X11 enabled, unbranded.
Codec and security settings remain upstream defaults. `gn gen` rejects unused
arguments, and `autoninja -j 4` maps to four local Siso jobs at this tool pin.
Four jobs are a conservative starting point. After observing low memory pressure
and 12 GiB available RAM, this machine's active build was safely interrupted and
resumed with `python3 mb/tools/upstream.py build --jobs 8`, reusing completed
outputs. After the heavy V8 compilation finished and about 11 GiB RAM was
available, it was resumed at ten jobs. A subsequent compiler-memory check found
about 3.6 GiB combined RSS and little CPU contention, allowing twelve jobs while
retaining memory headroom. The current command and logs are in STATUS.md.

The first output is intended at `src/out/mb-debug`. Record the active stage in
STATUS.md before compilation and watch free disk/RAM during the build. The
25 GiB entry reserve is not a guarantee the whole build fits.

The dependency-verification checks are covered by isolated local Git tests:

```sh
python3 -m unittest -v mb.test.test_upstream_tools
```

All eight tests passed on 2026-09-08. These are setup-tool tests; they do not
establish browser or product UI correctness. Artifact archives are hash-checked
by upstream gclient when downloaded; the stage tool verifies nested Git source
and does not rehash every extracted toolchain file before each invocation.

Launch using `--ozone-platform=x11` and a newly created private test user-data
directory. Follow [the baseline test protocol](testing.md) to verify the runtime
sandbox and real X11 behavior. Never use existing Chromium data for smoke tests.
The scoped driver is ready to run after a successful build receipt exists:

```sh
python3 mb/tools/upstream_smoke.py --build-receipt .build/logs/upstream-build-TIMESTAMP.json
```

It checks the binary's SHA-256 against the receipt and revalidates source pins,
then targets only the new test browser's PID/window. Screenshot and sandbox
evidence still require inspection before the baseline gate can pass.

The upstream baseline has now passed on the actual Arch/Xorg machine. Product
GN integration and its C++/companion tests also pass; see the [product commands](product-integration.md).
The renamed debug browser built and passed its scoped Xorg launch. Release output will use `src/out/mb-release`;
optimized build commands and Arch packaging remain pending.

## Product packaging requirements under preparation

Chromium's development defaults set `generate_about_credits = is_official_build`.
Consequently the retained unmodified debug baseline used a sample credits page.
Product debug arguments explicitly enable real credits; those have generated
successfully and include the product-vendored toml++ notice.
Before distributing any product build, explicitly generate the real upstream
credits and include the product's third-party notices. Enabling real credits is
required for the product debug build as well as the release build; it does not
require enabling Google branding. The implementation seam is
`components/resources/BUILD.gn`, including its downstream notice-directory hook.

The completed package must use an explicit payload inventory derived from the
effective GN settings and upstream Linux installer rules. The current debug
target's broad `runtime_deps` list includes thousands of generated/tool/test
inputs, so it is not an install manifest. Required browser resources, locales,
ICU/V8 data, Crashpad, enabled ANGLE/Vulkan/SwiftShader libraries and component
preloads must be accounted for. A component debug distribution also needs its
actual private ELF dependency closure and dynamically loaded libraries. Keep
those private libraries with the browser rather than installing them globally.

The sandbox target is a separate packaging prerequisite: build `chrome_sandbox`
and install it beside the browser as `chrome-sandbox`. Chromium's packaged
setuid fallback requires root ownership and mode 4755; package installation is
the privileged step, not the build agent. Renaming the browser executable does
not rename this helper's lookup path. Validate its dependencies and sandboxed
runtime after installation. Do not work around a missing helper by disabling
the sandbox.

Preserve upstream internal resource filenames when changing the executable's
identity. Staging must fail when a feature is enabled but its required payload
is absent, rather than silently omitting it. The eventual tested PKGBUILD,
release arguments, artifact inventory and exact commands remain Phase 6 work.
See [upstream maintenance](upstream-updates.md) for the update procedure and its
current validation limits.

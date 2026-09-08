# Implementation status

Updated: 2026-09-08.

- Current phase: Phase 1; required packages verified, fetching pinned upstream.
- Last successful browser build: none.
- Last successful browser test: none.
- Last successful tests: `python3 -m unittest -v mb.test.test_upstream_tools`;
  eight isolated setup-tool tests passed on 2026-09-08 (9.1 seconds).
- Last successful tooling checks (2026-09-08): Python byte-compilation and CLI
  help passed; pin TOML parsed; doctor/fetch returned the expected missing-gperf
  failure with no checkout created. Source fetching itself remains untested.
- Upstream: Chromium Linux stable 152.0.7977.82,
  `d04cdb24d67b081f6cf80200ffc5233f44b61109`, independently matched using Gitiles
  and `git ls-remote` against its stable tag.
- Source checkout: pinned Chromium Git checkout complete; gclient dependency sync
  in progress. Chromium modifications: none.
- Tool bootstrap: pinned depot_tools Python 3.11.8 installed and verified;
  depot_tools HEAD remains pinned and tracked source is clean.
- Blockers: none currently. User confirmed gperf installation; preflight passes.
- Active operation: `python3 mb/tools/bootstrap.py fetch`, log `.build/logs/fetch-01.log`.
- GitHub: `https://github.com/mattmezza/mb.git` configured as origin; initial
  setup checkpoint being prepared. Source and build artifacts remain ignored.

## Exact continuation

From this repository, inspect the active fetch/log before retrying:

```sh
python3 mb/tools/bootstrap.py doctor
python3 mb/tools/bootstrap.py fetch
python3 mb/tools/upstream.py hooks
python3 mb/tools/upstream.py gen
python3 mb/tools/upstream.py build --jobs 4
```

Pinned hooks and GN definitions have been inspected; stage commands are ready
but untested until sync completes. Run sequentially, then launch on Xorg with
sandbox enabled using docs/testing.md. No product
patches are allowed before that gate passes. Fetch and build logs must be kept
under `.build/`, and this file updated before each long-running build.

## Remaining work

All browser acceptance criteria are pending. Follow
[the phase plan](docs/implementation-plan.md): upstream build and launch, central
branding, native vertical tabs, TOML configuration and isolated environments,
capability verification, release build and tested Arch packaging. Maintain the
remaining product documentation as each implementation becomes concrete.

Resource watch: initial disk free 146 GiB; RAM 30 GiB with 14 GiB available;
swap 47 GiB with 28 GiB free. `/tmp` is nearly full tmpfs; use `.build/tmp`.
Two builds plus browser-test targets may need more space than the initial
checkout/build; recheck before each milestone. No unrelated files were removed.

# Implementation plan

This plan describes pending work. `STATUS.md` records the current phase, exact resume command, and latest observed build/test results. A milestone passes only when its evidence is recorded; planned behavior is not acceptance evidence.

## Working rules

- Preserve unrelated files and changes, including `.tmux-session`.
- Keep the pinned upstream checkout in `.build/chromium/src` and `depot_tools` in `.build/depot_tools`.
- Prove an unmodified upstream build and sandboxed Xorg launch before changing Chromium.
- Keep product code under `//mb/`, with narrow documented Chromium integration patches.
- Keep Chromium's browser services authoritative for tabs, profiles, extensions, DevTools, security, and storage.
- Build and test each integration milestone. Do not accumulate a large uncompiled patch.
- Record evidence, commands, resource measurements, limitations, and the next action in `STATUS.md` before long builds and whenever work stops.

## Phase 0: discovery and reproducibility setup

Status: completed on 2026-09-08; dependencies installed and verified.

Record machine and repository inspection, verify official Linux stable metadata and its exact Git tag/commit, and save the pin in `mb/upstream-version.toml`. Create fetch/build orchestration and the written progress record. Request the missing official Arch package `gperf`; wait for the user's installation confirmation, then verify it.

Exit evidence: recorded upstream identity and inspection results; required packages verified. Discovery is described in [the machine record](discovery-2026-09-08.md).

## Phase 1: unmodified upstream gate

Status: passed on 2026-09-08; see [the baseline review](upstream-baseline-2026-09-08.md).

Fetch using `depot_tools` and the exact Chromium pin. Inspect requirements from the pinned source, run its dependency synchronization and hooks, and record actual tool revisions. Generate a conservative development build at `out/mb-debug`, initially using four build jobs. Keep temporary files on disk and recheck free space before sync/build.

Compile unmodified Chromium. Launch under the real Xorg session with Ozone's X11 backend and the sandbox enabled, using a dedicated test user-data root. Record exact GN arguments, commands, build output, sandbox evidence, and startup/shutdown results. Do not use `--no-sandbox`.

Exit evidence: successful upstream compilation, functional Xorg browser window, verified sandbox, clean shutdown. Product integration remains gated until these checks pass. Independently compiled product libraries and test fixtures may be prepared outside the unmodified checkout.

## Phase 2: product skeleton and identity

Create the `//mb/{app,browser,common,config,ui,resources,test,tools}` product layer. Introduce one authoritative TOML branding manifest and derive build identity, executable name, headers, strings, desktop metadata, packaging values, and an original temporary icon. Document every necessary upstream patch and preserve Chromium licenses, credits, and third-party notices.

Exit evidence: branding generator tests pass, the renamed browser builds and launches, and a manifest-only name change regenerates the intended derived identity without manual code edits.

## Phase 3: native vertical tabs

Introduce narrow browser/window/tab/command abstractions backed by `Browser`, `TabStripModel`, `Profile`, and `WebContents`. Retain the working omnibox and navigation controls. Replace the visible horizontal strip with a Views sidebar in small compiled increments: tab display and operations; selection and status; pinned tabs/groups/context menus; drag reordering; scrolling and keyboard accessibility; collapse/resize persistence.

Include favicons, loading/crash/audio/mute states, incognito distinction, accessible names, and familiar Chromium shortcuts. Use Chromium's existing close/reopen and tab-management paths.

Exit evidence: product model/view tests and browser tests pass; keyboard, mouse, focus, resizing, and drag-and-drop work under Xorg; the sidebar remains responsive with at least 100 tabs. Record measured responsiveness rather than assuming it.

## Phase 4: configuration and environments

Implement a versioned TOML schema with strict type/key validation, filename/key diagnostics, safe path expansion, XDG defaults, a config override, and a validation-only command. Add examples and document reload/restart semantics. Keep secrets out of the main config.

Map every named environment to an independent Chromium user-data root before process/profile initialization. Create directories with restrictive permissions, detect unsafe root collisions, preserve Chromium's process singleton/activation behavior, and expose environment listing/path lookup. Use genuine off-the-record profiles for incognito. Persist sidebar state without corrupting user configuration or environment data.

Exit evidence: parsing, branding, path, isolation, and locking tests pass; malformed configuration fails safely; two running named environments demonstrate distinct cookies, history, and extension state; incognito persistence checks pass.

## Phase 5: preserved browser capabilities

Exercise a local Manifest V3 extension through loading, enable/disable/removal, action UI, service worker, content script, storage, environment isolation, and incognito permissions. Separately investigate legitimate Chrome Web Store installation and document any exact service or policy limitation.

Verify DevTools shortcuts, page-context Inspect, major panels, docking modes, normal/incognito windows, and extension debugging. Verify downloads, permission prompts, bookmarks, history, password storage integration, media, and session restoration. Exercise X11 multiple windows, clipboard, IME, HiDPI, available monitors, file picker, notifications where available, URL launch/activation, and desktop integration.

Exit evidence: automated smoke/browser tests and dated manual results identify what passed, failed, or could not be tested. Fix integration regressions before progressing.

## Phase 6: release and maintenance

Build the optimized configuration at `out/mb-release`, subject to measured disk capacity. Create and test the Arch packaging definition, including required helpers, resources, locales, sandbox permissions, desktop/MIME metadata, icons, and licenses. Run release-mode smoke tests from the installed package with the sandbox enabled.

Complete build, architecture, configuration, environment, security, extension, update, testing, and limitations documentation, plus the changelog. Document package replacement as the update mechanism and a repeatable stable-upstream update/rebase/conflict workflow. Explain each disabled upstream feature's security implications and unavailable Google-dependent services.

Exit evidence: tested release build and Arch package; reproducible instructions; all acceptance criteria linked to recorded results; explicit remaining limitations. Do not label the result a daily-driver alpha before all required criteria pass.

## Acceptance tracking

All acceptance evidence is currently pending.

| User criteria | Evidence owner |
| --- | --- |
| 1–2: reproducible Arch build, sandboxed Xorg launch | Phases 1 and 6 |
| 3–4: centralized identity, original non-Google branding | Phase 2 |
| 5–6: complete vertical sidebar, pinned/reordered tabs | Phase 3 |
| 7–10: safe configuration and environment isolation | Phase 4 |
| 11: genuine incognito | Phases 4 and 5 |
| 12–15: extensions, DevTools, ordinary browser features | Phase 5 |
| 16–18: tested release/package and upstream update instructions | Phase 6 |
| 19: product-owned tests pass | Each product milestone and final release |
| 20: explicit known limitations | Every phase and final report |

Pause only for required privileged package installation, inadequate resources, required credentials/accounts, an unavoidable destructive action, or a materially irreversible product decision. Continue independent authorized work while a dependency gate is pending, without crossing the unmodified-upstream gate.

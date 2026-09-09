# Implementation status

Updated 2026-09-09, Europe/Zurich. Current phase: 4; daily-driver alpha incomplete.
User authorized unrestricted computer use and requested minimal token spending.

## Verified checkpoint

- Production build: `.build/logs/product-build-20260909T112714.948212Z.json`.
- Focused test build: `.build/logs/product-test-build-20260909T112612.267737Z.json`.
- Native config/core tests: 65 plus 17 companion checks, `product-unit-20260909T112820.561762Z`.
- Eight executable configuration-error checks passed, `browser-config-gate-20260909T112834.695783Z`.
- Native fallback prevention passed, `environment-fallback-2plcetxt`.
- Sidebar/tab regression: four passed, `product-browser-tests-20260909T113556.369035Z`.
- Two configured Xorg environments and native same-environment activation passed,
  `two-env-odvs8nro`; both exited0 through native POSIX SessionEnding.
- Initial100-tab diagnostic passed, `product-browser-tests-20260909T093527.729468Z`;
  all loaded in77.9s, one network launch, no exits. Production/release latency remains open.
- Promoted environment/error/fallback harnesses:13 pure tests passed. Their root
  copies require prepare before the next receipt-bound build/run.

## Pending work

1. Finish production build for tested UI config0010, then integrate startup URLs/close-button patch0011.
2. Integrate prepared XDG config/bootstrap/remembered-selection core, preserving the
   stateless early URL validator now in root (never copy an older GURL-based gate).
3. Cookie/history/extension isolation and OTR persistence tests; prepared MV3/OTR tests remain uncompiled.
4. Remaining browser capability and physical Xorg input checks, optimized build,
   tested Arch packaging and update workflow.

## Limits and blockers

- No external blocker currently. Last disk check75GiB free; debug output36GiB.
  Release peak storage is unmeasured. No sudo or user-file deletion performed.
- Window-manager focus/close automation repeatedly failed. Environment lifecycle
  uses native SIGTERM/SessionEnding; it is not keyboard/window-close acceptance.
- /tmp intermittently reports quota exhaustion. Use an owned short TMPDIR beneath
  `.build/tmp`; preserve unrelated temporary files.
- Debug100-tab activation remains slow. No release performance claim.
- Explicit --config/--environment works; default XDG selection and UI application
  are pending. Do not claim complete environment-data isolation yet.
- Unrelated root `test/` and `.tmux-session` are untouched.

## Exact next commands

Patch0010 and its GN additions are now in root; compilation pending:

```sh
export TMPDIR=/home/matteo/dev/mb/.build/tmp
python3 mb/tools/product.py prepare
python3 mb/tools/product.py gen
python3 mb/tools/product.py test-build --targets mb:mb_browser_tests
```

Run `MbRuntimeUiConfigBrowserTest.*` through `mb/tools/browser_tests.py` using that
successful matching receipt. Prepared sources: `.build/tmp/runtime-ui-config-rw9y5a3z`.
Other prepared work: `.build/tmp/runtime-ui-actions-3xnclkkd`,
`.build/tmp/default-config-state-20260909T110623`, `.build/tmp/arch-payload-20260909`.
Build/test failures and prior evidence are retained; see docs/testing.md.

UI config0010: build114007.422487Z and four browser tests114210.015824Z passed.
Production build114329.749888Z passed. Native width/collapse/theme and synchronous multiwindow
restoration are covered; asynchronous extra-profile restores remain limited.

Patch0011 copied into root; startup URL/close-button compilation pending.

UI actions0011 compiled115014.248455Z. Configured URLs/close visibility, native
pins, OTR, session restoration and existing-process behavior passed114746;
corrected explicit-URL fixture passed115134.422351Z. Production rebuild pending.

Production build115205.086172Z passed for0011. Next: default XDG config/state.

Default XDG helper/gate integrated; native compilation pending. Earlier staging
script stopped at a test formatting mismatch; complete GN/test additions now in root.

Latest checkpoint: production115742.544580Z;73 native +17 companion passed115635.362056Z. Default XDG smoke default-config-_82bbob9 and eight negatives115834.843829Z passed. UI0010/0011 and default XDG are implemented. Explicit --config remembered selection remains open. Next: compile prepared MV3/OTR tests.

# Implementation status

Updated 2026-09-10, Europe/Zurich. Current phase: 5/6; daily-driver alpha incomplete.

## Verified

- Chromium 152.0.7977.82 is pinned at `d04cdb24d67b081f6cf80200ffc5233f44b61109`.
- The upstream and product debug builds launch under Xorg with Chromium's sandbox enabled.
- Central branding, native vertical tabs, strict TOML configuration, XDG defaults,
  isolated environment roots, startup URLs, sidebar preferences, genuine off-the-record
  profiles, MV3 extensions, and DevTools integration are compiled and tested.
- 73 native product tests and 17 companion checks passed in the latest full core run.
- Five MV3/incognito browser tests passed in
  `.build/test-evidence/product-browser-tests-20260909T120355.588766Z`.
- Cross-restart `personal`/`work` isolation for cookies, history, installed extensions,
  and extension storage passed in
  `.build/test-evidence/product-browser-tests-20260910T102805.399597Z`.
- Latest focused build receipt:
  `.build/logs/product-test-build-20260910T102637.730251Z.json`.
- Latest production debug build receipt:
  `.build/logs/product-build-20260909T115742.544580Z.json`.

## Pending

1. Integrate and test the Arch packaging definition.
2. Generate and compile `out/mb-release`; run release smoke and 100-tab checks.
3. Complete targeted browser-capability checks and record physical Xorg checks that
   require reliable user input.
4. Reconcile final documentation, known limitations, and upstream-update rehearsal.
5. Push the verified commits to GitHub.

## Limits

- About 75 GiB was free at the last check, below the preferred 100 GiB Chromium margin.
  No external blocker is active; release-build disk use remains to be measured.
- The current window manager does not reliably accept synthetic focus/input, so physical
  clipboard, IME, multi-monitor, and file-picker checks still need manual confirmation.
- Explicit `--config` selection is intentionally stateless today; remembered environment
  selection is implemented for the default XDG configuration path.
- Debug 100-tab activation remains slow; release performance is not measured yet.
- Unrelated root `test/` and `.tmux-session` are untouched.

## Continue

```sh
python3 mb/tools/product.py prepare
python3 mb/tools/product.py gen --profile release
python3 mb/tools/product.py build --profile release
```

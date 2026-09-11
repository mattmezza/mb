# Experimental testing

The optimized browser is usable for exploratory testing. It is not yet the
accepted daily-driver alpha because the installed package and remaining physical
desktop checks are incomplete. See [known limitations](known-limitations.md).

## Start a separate trial

From the repository root, derive the executable from the branding manifest and
create a new private temporary profile:

```sh
trial_dir="$(mktemp -d /tmp/mb-trial.XXXXXX)"
trial_executable="$(python3 -c 'import tomllib; print(tomllib.load(open("mb/branding.toml", "rb"))["product"]["executable_name"])')"
printf 'Trial profile: %s\n' "$trial_dir"
".build/chromium/src/out/mb-release/$trial_executable" \
  --ozone-platform=x11 \
  --user-data-dir="$trial_dir" \
  --no-first-run \
  --no-default-browser-check
```

This keeps the Chromium sandbox enabled and avoids using an existing personal
browser profile. The temporary directory may disappear during system cleanup;
keep important data in your regular browser. Do not import a daily-use profile.
The executable and its shared libraries must come from the same successful
product build. Close every experimental window before staging or rebuilding;
the build tool also checks for live output processes as a preflight.

The active build receipt and current testing availability are recorded in
[STATUS.md](../STATUS.md). To test named environments, copy
`mb/config/example.toml`, change its data directories to disposable absolute
paths, validate it with `mbctl`, then launch with `--config` and `--environment`.
Do not point a test configuration at an existing Chromium or Chrome profile.

## Useful checks

- Create three tabs with Ctrl+T; select, close with Ctrl+W, and reopen with
  Ctrl+Shift+T. Verify the restored page and sidebar count.
- Try Ctrl+Tab, Ctrl+Shift+Tab and Ctrl+L. Check that typing goes into the
  intended control after switching tabs.
- Use the tab context menu to pin a tab, drag tabs to new positions, and create
  a tab group. Check visible selection and order against the page being shown.
- Collapse, expand and resize the sidebar. Open another window and check the
  resulting width/state. Note both the action and what persisted.
- Open DevTools with F12 or Ctrl+Shift+I and with page-context Inspect. Try
  docking and undocking, Console and Elements.
- Open a private window with Ctrl+Shift+N and look for its private indication.
  Focused automated tests already cover history/cookie non-persistence; report
  any visible or behavioral inconsistency.
- Try ordinary sites, downloads, and audio/video. Record the specific failure;
  proprietary codecs and Google-dependent services are not promised.

For local storage/media/download fixtures, run this optional development server
in a second terminal:

```sh
python3 mb/tools/serve_test_pages.py --port 8000
```

Open `http://127.0.0.1:8000/` in the trial browser. The server binds only loopback,
serves fixed fixture files, and suppresses request logging. Stop it with Ctrl+C
when finished. Marker storage here can exercise local behavior but does not yet
supplement the automated named-environment isolation test. It is a testing tool,
not a browser runtime dependency.

## Report a finding

Include the build/commit if known, numbered reproduction steps, expected result,
actual result, and whether it happens again in a fresh trial profile. A screenshot
helps for visual issues; redact private content. Mention normal/private mode,
window scaling, and input method if relevant. Keep crashes and failed runs in the
record rather than treating a successful retry as their explanation.

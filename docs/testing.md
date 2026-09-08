# Browser test protocol

Status as of 2026-09-08: Phase 1 is fetching pinned upstream Chromium stable
152.0.7977.82. There has been no successful browser build and no successful
browser test. Every result below is pending. This document is a procedure, not
evidence that any command has run or passed.

## Phase 1 baseline gate

Use this protocol only after the unmodified binary exists at
`.build/chromium/src/out/mb-debug/chrome`. Run it from the repository root in an
interactive Bash shell attached to the real Xorg session. Start that shell with
`bash`, then enable `set -euo pipefail` as shown so a failed ownership, path, or
existence check stops the procedure. The test must use
`DISPLAY=:0`, Ozone's X11 backend, Chromium's normal sandbox, and a fresh,
dedicated user-data directory. Do not add `--no-sandbox`,
`--disable-setuid-sandbox`, `--remote-debugging-port`, or
`--remote-debugging-pipe`. No browser automation connector is part of this
baseline.

The operator must create an evidence directory for this run and record the exact
binary before launch. Replace the example timestamp with the actual UTC start
time. Do not overwrite an earlier evidence directory.

```sh
set -euo pipefail
export DISPLAY=:0
CHROME="$PWD/.build/chromium/src/out/mb-debug/chrome"
TEST_DATA="$PWD/.build/test-data/upstream-baseline"
RUN_ID=20260908T000000Z
EVIDENCE="$PWD/.build/test-evidence/upstream-baseline-$RUN_ID"

mkdir -p "$PWD/.build/test-evidence"
mkdir "$EVIDENCE"
command -v xdotool xprop xwininfo import ps
test "${DISPLAY:-}" = :0
xprop -root _NET_SUPPORTING_WM_CHECK | tee "$EVIDENCE/xorg-root.txt"
xwininfo -root | tee "$EVIDENCE/xorg-root-info.txt"
test -x "$CHROME"
test ! -e "$TEST_DATA"
mkdir -m 700 -p "$PWD/.build/test-data"
mkdir -m 700 -- "$TEST_DATA"
test "$(stat -c '%a' "$TEST_DATA")" = 700
readlink -f "$CHROME" | tee "$EVIDENCE/binary-path.txt"
sha256sum "$CHROME" | tee "$EVIDENCE/binary-sha256.txt"
"$CHROME" --version | tee "$EVIDENCE/browser-version.txt"
```

Record the build command, its zero exit status, the GN arguments, and the
complete build log in the same evidence directory. Also capture and review the
source identity before treating the binary as upstream:

```sh
git -C .build/chromium/src rev-parse HEAD | tee "$EVIDENCE/source-head.txt"
git -C .build/chromium/src status --short --untracked-files=no \
  | tee "$EVIDENCE/source-status.txt"
```

`source-head.txt` must contain the pinned stable commit
`d04cdb24d67b081f6cf80200ffc5233f44b61109`, and `source-status.txt` must be
empty. A stale binary, a dirty checkout, a missing build log, or an unrecorded
nonzero build exit status does not satisfy the unmodified-build prerequisite.

If `.build/test-data/upstream-baseline` already exists, stop and inspect it. Do
not delete or reuse it casually: a clean profile is part of the baseline. Move
an earlier profile aside under `.build/test-data/` with a dated name only after
confirming that it belongs to this test. Never point this procedure at
`~/.config/chromium`, another normal profile, or a directory containing user
data.

Launch only the pinned, unmodified build and retain its browser PID. The launch
log and command-line inspection are part of the evidence.

```sh
DISPLAY=:0 "$CHROME" \
  --ozone-platform=x11 \
  --user-data-dir="$TEST_DATA" \
  --no-first-run \
  --no-default-browser-check \
  about:blank >"$EVIDENCE/chrome.log" 2>&1 &
CHROME_PID=$!

printf '%s\n' "$CHROME_PID" | tee "$EVIDENCE/browser-pid.txt"
sleep 2
kill -0 "$CHROME_PID"
readlink -f "/proc/$CHROME_PID/exe" | tee "$EVIDENCE/browser-exe.txt"
tr '\0' ' ' <"/proc/$CHROME_PID/cmdline" | tee "$EVIDENCE/browser-cmdline.txt"
printf '\n' >>"$EVIDENCE/browser-cmdline.txt"
```

Check that `browser-exe.txt` is the same resolved path as `binary-path.txt`, and
that `browser-cmdline.txt` contains the exact dedicated user-data directory and
`--ozone-platform=x11`. It must contain no sandbox-disabling or remote-debugging
flag.

## Bind all actions to the test window

Find the X11 window from the captured browser PID. Never use a broad search such
as `xdotool search --class Chromium`, and never send keys to whichever window is
currently active. Those approaches can operate on an existing user browser.

```sh
TEST_WINDOW_ID=$(xdotool search --sync --onlyvisible --pid "$CHROME_PID" | head -n 1)
test -n "$TEST_WINDOW_ID"
xprop -id "$TEST_WINDOW_ID" _NET_WM_PID WM_CLASS WM_NAME \
  | tee "$EVIDENCE/window-properties.txt"
xwininfo -id "$TEST_WINDOW_ID" | tee "$EVIDENCE/window-info.txt"
```

Confirm that `_NET_WM_PID` equals `CHROME_PID` before continuing. Keep
`TEST_WINDOW_ID` for every `xdotool`, `xprop`, `xwininfo`, and ImageMagick
`import` invocation. If another browser window appears in a PID search, inspect
its `_NET_WM_PID` and record it as another test-owned window before touching it.
If the PID check fails, stop without interacting with that window.

Capture the initial visible state:

```sh
import -window "$TEST_WINDOW_ID" "$EVIDENCE/01-startup.png"
xprop -id "$TEST_WINDOW_ID" WM_NAME | tee "$EVIDENCE/01-startup-title.txt"
```

## Genuine sandbox verification

A successful compile, an omitted `--no-sandbox` flag, or a browser window alone
does not prove that the sandbox is active. Collect both browser-reported and
kernel-reported runtime evidence.

First navigate the PID-bound test window to Chromium's sandbox diagnostics:

```sh
xdotool key --window "$TEST_WINDOW_ID" --clearmodifiers ctrl+l
xdotool type --window "$TEST_WINDOW_ID" --clearmodifiers --delay 1 'chrome://sandbox'
xdotool key --window "$TEST_WINDOW_ID" --clearmodifiers Return
sleep 2
import -window "$TEST_WINDOW_ID" "$EVIDENCE/02-chrome-sandbox.png"
xprop -id "$TEST_WINDOW_ID" WM_NAME | tee "$EVIDENCE/02-sandbox-title.txt"
```

The operator must inspect the rendered `chrome://sandbox` page and record every
reported sandbox status, including unavailable layers. The baseline fails if
the page says the renderer sandbox is disabled or if the process was launched
with a sandbox-disabling flag.

Then recursively identify descendants of the captured browser PID and record
Linux sandbox state. Do not match processes by executable name alone. The global
inventory deliberately contains only PIDs and PPIDs; command lines are read only
for test-browser descendants, so this procedure does not collect URLs or flags
from the user's other browser processes.

```sh
ps -e -o pid=,ppid= | tee "$EVIDENCE/process-pids-ppids.txt"
: >"$EVIDENCE/descendant-pids.txt"

collect_descendants() {
  local PARENT_PID=$1 CHILDREN_FILE CHILD_PID
  CHILDREN_FILE="/proc/$PARENT_PID/task/$PARENT_PID/children"
  test -r "$CHILDREN_FILE" || return 0
  for CHILD_PID in $(<"$CHILDREN_FILE"); do
    printf '%s %s\n' "$CHILD_PID" "$PARENT_PID" \
      >>"$EVIDENCE/descendant-pids.txt"
    collect_descendants "$CHILD_PID"
  done
}

collect_descendants "$CHROME_PID"

while read -r PID PPID; do
  test -r "/proc/$PID/status" || continue
  tr '\0' ' ' <"/proc/$PID/cmdline" \
    >"$EVIDENCE/process-$PID-cmdline.txt"
  printf '\n' >>"$EVIDENCE/process-$PID-cmdline.txt"
  sed -n '/^Name:/p;/^Pid:/p;/^PPid:/p;/^NSpid:/p;/^NoNewPrivs:/p;/^Seccomp:/p;/^Seccomp_filters:/p' \
    "/proc/$PID/status" >"$EVIDENCE/process-$PID-status.txt"
done <"$EVIDENCE/descendant-pids.txt"
```

Chromium may place renderer processes below a zygote rather than directly below
the browser. Find a saved descendant command line containing `--type=renderer`
and use `descendant-pids.txt` to trace that PID's recorded parent chain to
`CHROME_PID`. Record whether its saved status contains `Seccomp: 2`, one or more
seccomp filters, namespace identifiers, and `NoNewPrivs`. Interpret these fields
together with `chrome://sandbox`; absence of an optional layer is not silently
converted into a pass. Save the observed values and conclusion in `results.md`
inside the evidence directory.

## Normal UI smoke checks

All input remains addressed to `TEST_WINDOW_ID`. After each navigation, wait for
the visible page to settle, capture a screenshot, and record the window title.
Screenshots are observed evidence: the operator or agent must visually inspect
them to confirm the stated UI.

1. Navigate to `chrome://version`. Confirm the displayed Chromium version,
   executable path, command line, and profile path correspond to this build and
   `TEST_DATA`. Capture `03-version.png`.
2. Navigate to
   `data:text/html,<title>MB%20baseline</title><h1>Navigation%20works</h1>`.
   Confirm the heading renders and capture `04-navigation.png`.
3. Press `Ctrl+T`; confirm a new tab appears. Navigate it to `about:blank`, press
   `Ctrl+Tab` and `Ctrl+Shift+Tab`, and confirm selection moves between the two
   tabs. Close only the selected test tab with `Ctrl+W`. Capture
   `05-tabs.png` before closing and record titles after each switch.
4. Press `F12` in the test window. Confirm DevTools opens for the test page and
   that Elements, Console, Sources, Network, and Application panels are present
   and selectable. Confirm docking controls respond, without changing the
   user's global browser. Capture `06-devtools.png`, then press `F12` again and
   confirm it closes.

Use this pattern for each address and key; substitute the requested value and
evidence filename:

```sh
xdotool key --window "$TEST_WINDOW_ID" --clearmodifiers ctrl+l
xdotool type --window "$TEST_WINDOW_ID" --clearmodifiers --delay 1 'chrome://version'
xdotool key --window "$TEST_WINDOW_ID" --clearmodifiers Return
sleep 2
import -window "$TEST_WINDOW_ID" "$EVIDENCE/03-version.png"
xprop -id "$TEST_WINDOW_ID" WM_NAME >>"$EVIDENCE/ui-titles.txt"
```

Do not infer page success from `xdotool` returning zero. A zero exit status only
shows that the input was delivered. The screenshot, title, and visual inspection
establish whether the requested browser behavior occurred.

## Clean shutdown

Close the test browser through its normal UI, then verify that the captured
browser process exits. Reconfirm the window's PID immediately before sending the
close shortcut.

```sh
xprop -id "$TEST_WINDOW_ID" _NET_WM_PID | tee "$EVIDENCE/shutdown-window-pid.txt"
xdotool key --window "$TEST_WINDOW_ID" --clearmodifiers ctrl+shift+w

for ATTEMPT in 1 2 3 4 5 6 7 8 9 10; do
  kill -0 "$CHROME_PID" 2>/dev/null || break
  sleep 1
done

if kill -0 "$CHROME_PID" 2>/dev/null; then
  printf '%s\n' 'FAIL: browser still running after normal close' \
    | tee "$EVIDENCE/shutdown-result.txt"
else
  if wait "$CHROME_PID"; then
    STATUS=0
  else
    STATUS=$?
  fi
  printf 'browser exited after normal close; wait status=%s\n' "$STATUS" \
    | tee "$EVIDENCE/shutdown-result.txt"
fi
```

If normal shutdown fails, preserve the logs and inspect the exact PID, executable
path, command line, and test profile before sending a targeted `TERM`. Never use
`pkill chrome`, `killall`, a class-wide window close, or any command that could
terminate the user's existing browser processes. Record a forced termination as
a failed clean-shutdown check.

The Phase 1 gate passes only when the unmodified build result, exact launch
command, PID/window ownership, sandbox diagnostics, renderer kernel state,
startup screenshots, normal UI observations, and clean shutdown result are all
recorded together and reviewed. Update `STATUS.md` with links or paths to the
evidence and any failures; the presence of files alone is not a pass.

## Pending evidence matrix

Browser rows remain pending while the first upstream build and launch are
incomplete. “Automated” means a repeatable product-owned test or command with an
asserted result. “Manual/observed” means dated direct inspection by an operator
or agent, supported by screenshots or logs.

| Area | Automated evidence | Manual/observed evidence | Current state |
| --- | --- | --- | --- |
| Upstream startup, navigation, tabs, shutdown | PID-scoped launch/process checks; future smoke assertions | X11 window, page, tab switching, clean-close observation | Pending Phase 1 |
| Sandbox | `/proc` renderer status and command-line capture | `chrome://sandbox` inspection | Pending Phase 1 |
| Product configuration | Parser/schema and compiled control-command tests pass | Browser restart/reload and error presentation | Standalone code tested; browser integration pending |
| Named environments | Path/collision/secure-creation tests pass; browser locking and storage tests pending | Two concurrent environment workflows | Standalone path core tested; browser integration pending |
| Product tab sidebar | Model/browser tests, keyboard operations, 100-tab measurements | Mouse, focus, resize, drag, state and accessibility checks | Pending Phase 3; product code absent |
| Incognito | Off-the-record and persistence assertions | Normal/incognito distinction and workflow | Pending Phases 4–5 |
| Extensions | MV3 load, service worker, content script, storage and isolation tests | Action UI, permissions, enable/disable/removal and extension DevTools | Pending Phase 5 |
| DevTools | Future browser tests for opening and inspected targets | Shortcuts, context Inspect, panels and docking in normal/incognito windows | Pending baseline smoke and Phase 5 depth |
| Linux/X11 integration | Targeted checks where Chromium exposes test hooks | Clipboard, IME, HiDPI, multiple monitors/windows, file picker, notifications, URL activation and desktop integration | Pending Phase 5 |

## Standalone product checks

Observed on this Arch machine on 2026-09-08:

| Command | Result |
| --- | --- |
| `python3 -m unittest mb.test.test_upstream_tools mb.test.test_branding -v` | 23 passed; includes generated/version-header compilation, actual pinned GN evaluation, and safe output migration |
| `python3 mb/tools/test_startup_arguments.py` | 8 GoogleTests passed |
| `python3 mb/tools/test_config.py` | 15 GoogleTests passed, including excessive nesting and malformed UTF-8 |
| `python3 mb/tools/test_environment_paths.py` | 22 GoogleTests passed, including concurrent creation, permissions, symlinks and path lengths |
| `python3 mb/tools/build_control.py` then `python3 -m unittest mb.test.test_control -v` | Compiled companion and 17 command-level tests passed |

The C++ checks use C++20, disabled exceptions/RTTI, and the pinned Clang. Outputs
are under `.build/`; no files are added to the Chromium checkout by these tests.
Parser and environment tests use the fetched Chromium GoogleTest source with
the host C++ standard library. GN/libc++ integration remains a later gate.

The parser's vendor patch was applied to a disposable copy of its pinned
upstream single header and reproduced the checked-in header byte for byte.
These results establish standalone behavior only; they do not establish browser
isolation, sandboxing, extension compatibility, or a working product UI.

## Local capability fixtures

```sh
python3 mb/tools/serve_test_pages.py --port 8000
```

Open `http://127.0.0.1:8000/` in the dedicated test browser. The server exposes
only fixed fixture routes, binds only loopback, and suppresses request logs.
The page can write/read fixed work/personal markers in cookies, localStorage,
and IndexedDB; clear only those fixture keys; download fixed text; play a local
one-second WAV; and request location permission after an explicit button click.
It does not store or display coordinates. Use the same origin in both browser
environments when comparing storage; different ports are different origins.

The [MV3 extension protocol](../mb/test/extensions/README.md) uses this page.
Its unpacked code has passed JSON/scope and Node syntax checks. The fixture
server's four route/content tests pass with
`python3 -m unittest mb.test.test_fixture_server -v`. Browser behavior for both
fixtures is pending; JavaScript syntax checks are not runtime verification.

# Browser test protocol

Latest standalone upstream-tool run: 21 tests passed with pinned Python on
2026-09-08, including 12 read-only candidate-checker tests. Receipt:
`.build/test-evidence/standalone-20260908T111509.757071Z/results.json`.
The live official-metadata check also matched the current Chromium pin; its
candidate record and comparison log are in
`.build/update-checks/20260908T111527.423218Z/`. These are maintenance-tool
results and do not establish browser runtime behavior.

Status as of 2026-09-08: the unmodified Chromium 152.0.7977.82 build and actual
Xorg baseline gate passed. See [the dated review](upstream-baseline-2026-09-08.md)
for navigation, tab operations, docked DevTools, sandbox and clean shutdown
observations, as well as the failed first attempt and remaining limits. Product
integration and broader capability acceptance remain pending.

## Phase 1 baseline gate

Use this protocol only after the unmodified binary exists at
`.build/chromium/src/out/mb-debug/chrome`. Run it from the repository root in an
interactive Bash shell attached to the real Xorg session. Start that shell with
`bash`, then enable `set -euo pipefail` as shown so a failed ownership, path, or
existence check stops the procedure. The test must use the real Xorg session
(`DISPLAY=:0` on the inspected machine), Ozone's X11 backend, Chromium's normal sandbox, and a fresh,
dedicated user-data directory. Do not add `--no-sandbox`,
`--disable-setuid-sandbox`, `--remote-debugging-port`, or
`--remote-debugging-pipe`. No browser automation connector is part of this
baseline.

`mb/tools/upstream_smoke.py --build-receipt PATH` automates the scoped launch,
navigation/tab assertions, kernel sandbox checks, screenshots and shutdown.
It opens direct test URLs via Alt+Enter, avoiding stock remote NTP UI; Ctrl+T
remains a product verification item. Base64 HTML avoids the recorded upstream
percent-encoded data-URL debug assertion. DevTools uses Ctrl+Shift+I with time
for its first frontend load. F12 now passed the first sidebar Xorg input check;
see [the dated review](sidebar-baseline-2026-09-08.md).
It uses the caller's `DISPLAY`; confirm this is the actual Xorg session, not a
virtual or unintended display. Its successful result still requires a dated
`results.md` in the evidence directory recording visual review of navigation,
DevTools and `chrome://sandbox`. Inspect additional owned DevTools windows if
undocked. A screenshot filename or the automated exit code alone does not pass
those checks. Record the browser-reported sandbox conclusion together with
the collected renderer kernel state before allowing product integration.
The driver excludes `CHROME_EXTRA_FLAGS` and channel variants from the test
child's environment because Chromium would otherwise append unrecorded switches.
Only excluded variable names are recorded; their values and the parent process's
environment remain untouched.

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
   `data:text/html;base64,PHRpdGxlPkJhc2VsaW5lIG9uZTwvdGl0bGU+PGgxPk5hdmlnYXRpb24gd29ya3M8L2gxPg==`.
   Confirm the heading renders and capture `04-navigation.png`.
3. Enter `about:blank` in the omnibox and press `Alt+Enter`; confirm a new tab
   appears with that local page. Stock regular NTP can fetch remote executable
   UI, so defer `Ctrl+T` until the product local NTP exists. Press
   `Ctrl+Tab` and `Ctrl+Shift+Tab`, and confirm selection moves between the two
   tabs. Close only the selected test tab with `Ctrl+W`. Capture
   `05-tabs.png` before closing and record titles after each switch.
4. Press `Ctrl+Shift+I` in the test window and allow its initial frontend to
   load. Confirm DevTools opens for the test page and
   that Elements, Console, Sources, Network, and Application panels are present
   and selectable. Confirm docking controls respond, without changing the
   user's global browser. Capture `06-devtools.png`, then press `Ctrl+Shift+I` again and
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

The upstream and branded debug build/launch gates passed. Deeper capability
checks remain pending as shown below. “Automated” means a repeatable product-owned test or command with an
asserted result. “Manual/observed” means dated direct inspection by an operator
or agent, supported by screenshots or logs.

| Area | Automated evidence | Manual/observed evidence | Current state |
| --- | --- | --- | --- |
| Upstream startup, navigation, tabs, shutdown | PID-scoped launch/process checks; future smoke assertions | X11 window, page, tab switching, clean-close observation | Passed scoped Phase 1 and Phase 2 gates |
| Sandbox | `/proc` renderer status and command-line capture | `chrome://sandbox` inspection | Passed scoped Phase 1 and Phase 2 gates |
| Product configuration | Parser/schema and compiled control-command tests pass | Browser restart/reload and error presentation | Standalone code tested; browser integration pending |
| Named environments | Path/collision/secure-creation tests pass; browser locking and storage tests pending | Two concurrent environment workflows | Standalone path core tested; browser integration pending |
| Product tab sidebar | Model/browser tests, keyboard operations, 100-tab measurements | Mouse, focus, resize, drag, state and accessibility checks | Initial sidebar browser/Xorg input checkpoint passed; scale and broader accessibility/status coverage pending |
| Incognito | Off-the-record and persistence assertions | Normal/incognito distinction and workflow | Pending Phases 4–5 |
| Extensions | MV3 load, service worker, content script, storage and isolation tests | Action UI, permissions, enable/disable/removal and extension DevTools | Pending Phase 5 |
| DevTools | Future browser tests for opening and inspected targets | Shortcuts, context Inspect, panels and docking in normal/incognito windows | Ctrl+Shift+I docked Elements observed; Phase 5 depth pending |
| Linux/X11 integration | Targeted checks where Chromium exposes test hooks | Clipboard, IME, HiDPI, multiple monitors/windows, file picker, notifications, URL activation and desktop integration | Pending Phase 5 |

## Standalone product checks

Run all standalone suites, or select focused suites, with:

```sh
python3 mb/tools/test_product.py --list
python3 mb/tools/test_product.py
python3 mb/tools/test_product.py --suite config --suite environments
```

After bootstrap, `.build/depot_tools/python-bin/python3` can replace `python3`
in these commands to use the downloaded, pinned Python interpreter.

The runner executes suites sequentially and keeps each command's log and exit
status in a timestamped `.build/test-evidence/standalone-*` directory. It fails
on the first unsuccessful command and requires fetched build inputs for suites
that use them. These receipts explicitly exclude browser acceptance. Individual
commands below remain useful during development.

Observed on this Arch machine on 2026-09-08:

| Command | Result |
| --- | --- |
| `python3 -m unittest mb.test.test_upstream_tools mb.test.test_branding -v` | 23 passed; includes generated/version-header compilation, actual pinned GN evaluation, and safe output migration |
| `python3 -m unittest mb.test.test_branding_assets -v` | 4 actual-renderer tests passed; native dimensions, deterministic bytes, and safe output handling |
| `python3 -m unittest mb.test.test_upstream_smoke -v` | Linux Chromium process-title regression passed; owned non-dumpable child behavior also checked on the actual kernel |
| `python3 mb/test/test_branding_strings.py` | 7 passed; pinned GRIT preserves 680 resource IDs and 142 German translations; checks attribution and English fallback |
| `python3 mb/tools/test_startup_arguments.py` | 8 GoogleTests passed |
| `python3 mb/tools/test_config.py` | 15 GoogleTests passed, including excessive nesting and malformed UTF-8 |
| `python3 mb/tools/test_environment_paths.py` | 22 GoogleTests passed, including concurrent creation, permissions, symlinks and path lengths |
| `python3 mb/tools/test_runtime_config.py` | 6 GoogleTests passed; selection precedence, useful errors/warnings, canonical config location, all-root audit and no directory creation |
| `python3 mb/tools/build_control.py` then `python3 -m unittest mb.test.test_control -v` | Compiled companion and 17 command-level tests passed |

The C++ checks use C++20, disabled exceptions/RTTI, and the pinned Clang. Outputs
are under `.build/`; no files are added to the Chromium checkout by these tests.
Parser and environment tests use the fetched Chromium GoogleTest source with
the host C++ standard library. These historical standalone receipts did not test GN/libc++; the subsequent
51 GN-built C++ tests passed as recorded in the product baseline.

The parser's vendor patch was applied to a disposable copy of its pinned
upstream single header and reproduced the checked-in header byte for byte.
These results establish standalone behavior only; they do not establish browser
isolation, sandboxing, extension compatibility, or a working product UI.

## Focused browser-test target

The product `//mb:mb_browser_tests` target uses the pinned
`//chrome/test:browser_tests_runner`, `test_support`, `test_support_ui`, and packed
browser resources, with direct dependencies for each test. It uses Chromium's
`InProcessBrowserTest` and launcher without the aggregate upstream test source list.
The first shared test-support build passed in 34m57s (2,925 actions).

```sh
python3 mb/tools/product.py prepare
python3 mb/tools/product.py gen
python3 mb/tools/product.py test-build --targets mb:mb_browser_tests --jobs 12
python3 mb/tools/browser_tests.py --build-receipt .build/logs/<successful-test-build>.json
```

The runner requires a matching integration and recorded test executable hash.
It uses fresh private TMPDIR/XDG paths, the real X11 display and one launcher job.
TMPDIR is a short 0700 directory beneath `.build/tmp`: a timestamped nested
profile path exceeded Linux's Unix socket limit in the first attempt. The
runner checks the expected socket path capacity before launch.
Chromium creates each test's user-data root beneath that temporary storage. Child
CHROME_EXTRA_FLAGS variables are removed; no sandbox-disabling switch is added.
Retries are disabled, and an empty, skipped, failed or malformed summary does
not pass. Evidence and launcher logs remain under `.build/test-evidence/`.

The first test passed in 12 seconds on actual X11, with evidence at
`.build/test-evidence/product-browser-tests-20260908T195515.267037Z/results.json`.
It covers local navigation and authoritative model creation,
activation, reordering and closing. New NTP/sidebar tests are prepared separately
and are not yet compiled or accepted. Test-support compilation is substantial;
subsequent focused builds reuse it.

The launcher bypasses the production executable's raw `chrome_main.cc` entry
point, so early argument normalization also needs real executable smoke tests.

The harness still runs the real `ChromeMainDelegate::BasicStartupComplete()` and
`PreSandboxStartup()`. Its `SetUpUserDataDirectory()` hook runs after Chromium
has created/overridden the temporary user-data root and before those callbacks.
Use that hook to write an explicit fixture TOML, select an environment pointing
at the same root, and append `--config`/`--environment` through CommandLine's
typed switch methods. The temporary root exists at this point; the Profile does
not. Tests must exercise the production validator rather than bypass its gate.

Upstream's harness does not itself redirect `XDG_CONFIG_HOME`; the product
runner supplies private XDG configuration, data and cache roots. Always supply
the fixture config explicitly when testing startup integration. `SetUpOnMainThread()` and local-state preference
setup are too late for this early gate. Renderer/utility test processes skip
fixture setup, so product configuration initialization must remain browser-only.
Any auxiliary `--launch-as-browser` test process needs its own explicit inputs.

## Local capability fixtures

```sh
python3 mb/tools/serve_test_pages.py --port 8000
```

Open `http://127.0.0.1:8000/` in the dedicated test browser. The server exposes
only fixed fixture routes, binds only loopback, and suppresses request logs.
The page can write/read fixed work/personal markers in cookies, localStorage,
and IndexedDB; clear only those fixture keys; download fixed text; play a local
one-second WAV and an original two-second VP8 WebM; and request location permission
after an explicit button click. The video uses native controls without autoplay.
Its 48 frames decode successfully with FFmpeg; browser playback remains pending.
It does not store or display coordinates. Use the same origin in both browser
environments when comparing storage; different ports are different origins.

The [MV3 extension protocol](../mb/test/extensions/README.md) uses this page.
Its unpacked code has passed JSON/scope and Node syntax checks. The fixture
server's five route/content tests pass with
`python3 -m unittest mb.test.test_fixture_server -v`. Browser behavior for both
fixtures is pending; JavaScript syntax checks are not runtime verification.


## DevTools and 100-tab measurements (2026-09-08)

Four focused DevTools cases pass: native command opening/closing, the actual
frontend's off-the-record browser context, docked/undocked windows, and the
native page-context Inspect command. These exercise native command paths;
actual F12 and Ctrl+Shift+I input are recorded in the sidebar Xorg review.
They do not establish every DevTools panel or extension-debugging workflow.
Test build: `.build/logs/product-test-build-20260908T211909.116247Z.json`.
Combined five-case run: `product-browser-tests-20260908T212019.167559Z`.

The scale case creates 100 real native-model tabs, checks the sidebar projection,
selects, reorders and closes a tab, then restores the count and selects after
all local pages finish loading. Correctness passes. Performance is not accepted.
The first run's GoogleTest properties were omitted by Chromium's custom XML
writer. Instrumentation now uses `base::AddTagToTestResult`; retain the native
`tags` in the launcher's `summary.json`.

Latest measured run: `product-browser-tests-20260908T213237.269243Z`;
build `.build/logs/product-test-build-20260908T213104.109228Z.json`.

| Operation | Debug wall time |
| --- | ---: |
| Insert 99 tabs into the initial window | 17.835 s |
| Subsequent projection/layout wait | 14.644 s |
| First selection plus projection/layout wait | 1.357 s |
| Reorder plus projection/layout wait | 0.768 s |
| Close plus projection/layout wait | 1.009 s |
| Loaded selections at indices 0 / 99 / 50 | 298 / 524 / 486 ms |

These are debug component-build samples, not compositor presentation timings
or a release-performance result. `base::test::RunUntil` first evaluates its
predicate at the next UI-thread idle, so the samples include queued browser
work. `RunScheduledLayouts` lays out all dirty widgets; it does not deliberately
sleep or await animation completion. The projection's model/anchor checks do
not prove the selected item has scrolled into view or reached a presented frame.
Further instrumentation will preserve these totals while separating native
operation, idle wait and actual layout costs. Slow debug behavior remains a
known issue until measured and addressed; a passing correctness assertion is
not a responsiveness pass.

## Sidebar state and keyboard coverage

The five-case run `product-browser-tests-20260908T215044.420082Z` passed
native AX names/selected state, controlled waiting/loading/completion indicators,
owned renderer crash/recovery on the same WebContents and tab view, and real
loopback WAV playback with native audibility/mute/unmute indicators. Audio used
the pinned upstream `chrome/test/data/media/pink_noise_140ms.wav`, an ordinary
test user gesture and the existing audio service. Crash allowance covered only
the test-owned renderer.

The Return-key case initially failed its post-activation focus assertion.
Activation succeeded. Inspection of BrowserView::OnActiveTabChanged confirmed
native selected-WebContents focus restoration; the revised case waits for the
local page to load and asserts focus inside the actual active contents view.
Both AX/keyboard cases and the scale case passed in
`product-browser-tests-20260908T215551.366276Z`. These are native view/state
checks, not AT-SPI/screen-reader certification, crash-icon pixel testing, or
pointer hit-testing of the mute button.

The expanded scale run passed native model/view/presentation correctness, while
performance remains open. Loaded native activation itself took 249 / 239 / 290 ms;
the first idle boundary added 44 / 196 / 224 ms. The subsequent forced all-widget
layout took 64 / 0.4 / 0.4 ms. A separate pass required actual TabView::IsActive
and full viewport containment, then a subsequent successful frame: 523 / 458 /
547 ms from activation start, with native presentation flags 0. These are
subsequent-frame checkpoints, not earliest-frame or hardware input latency.

## Bulk local-page navigation diagnostic

`product-browser-tests-20260908T221016.242372Z` passed all 100 data-page
URL/title/load checks after the initial browser was ready. Native mutation took
22.119 s and all pages completed by 53.066 s; the observed network service had
zero lifecycle events before intentional teardown. The recorder requires an
actual out-of-process service and reports native crash/kill/exit status if
observed. This narrows the original production startup failure but does not
reproduce its raw command-line/initialization sequence or prove fast navigation.
The outer runner used a 300-second bound; native waits were bounded per page and
by a three-minute workload deadline.

## Cleanup and native trace follow-up

`product-browser-tests-20260908T221539.543092Z` passed both cleanup cases and
the expanded 100-tab trace case. The first keyboard attempt, 220902.297417,
crashed in the uninitialized Ozone ui_controls test helper before key handling.
The corrected case uses the native Views EventGenerator, checks omnibox focus,
and observes actual local Return/Ctrl+Return navigation. It does not claim
OS-level input synthesis. The surface case checks the actual product NTP,
unavailable Customize Chrome entry, and omitted AI action/hints while the
upstream AI shortcut feature is enabled.

The native trace runs after all prior measurements and records fixed numeric
aggregates only. Trace data-loss/error count was 0; all expected markers/native
scopes were present, and traced self times partitioned each root duration.
For selection 0, native activation took 262 ms with 179 ms inside BrowserView's
active-tab callback; its UpdateUIForContents/BrowserView layout path accounted
for 115 ms. Inclusive nested durations overlap and must not be added. The other
two instrumented activations took 274 / 244 ms. These remain debug measurements
with tracing overhead, not an optimization or a release result.

Cleanup compilation failures are retained in build logs: 220029.782490 rejected
unreachable Linux menu code; 220346.523423 rejected test access to a private
helper, a missing interactive-input header and a vexing parse. Preprocessor
guards and the public LocationBar interface corrected them. Build 220731.524618
passed, followed by the recorded input-harness runtime failure. Build 221422.316201
and its three-case run passed after switching to native Views EventGenerator.


On 2026-09-09, the saved cleanup review (`sidebar-input-20260908T221951.174452Z`)
was found to have reached its bounded lifetime during the limits pause. The
helper terminated only its owned process group; this is not a clean shutdown
pass. A fresh review (`sidebar-input-20260909T072331.107633Z`) painted the local
product NTP with no inherited AI chip after clicking the owned window to focus
it. Desktop focus repeatedly moved elsewhere; subsequent local navigation timed
out and triggered scoped cleanup. The initial captures still showed about:blank,
so their filenames are not proof of NTP navigation. Capture `03-focused-ntp.png`
is the painted NTP. No keyboard or private-window pass is inferred from this run.

The matching NTP/sidebar regression run at
`product-browser-tests-20260909T072551.791965Z` passed all nine cases, using
`product-test-build-20260908T221422.316201Z.json`. This covers the local NTP
routing, extension override, genuine incognito NTP, blocked script execution,
and sidebar projection/orientation/geometry persistence cases.

Debug/release profile tooling was integrated on 2026-09-09. The direct profile,
integration, browser-runner and smoke helper suite passed 27 tests. The recorded
`standalone-20260909T073010.941754Z` integration-tools step passed 29 tests. Its
subsequent upstream-tools step failed one existing deep-JSON rejection case
under host Python 3.12; that parser boundary is being corrected explicitly.
No release GN generation, build, or browser launch is implied by tooling tests.

The JSON metadata fix adds explicit nesting and numeric-token bounds instead
of relying on interpreter recursion behavior. Quoted brackets, escaped quotes
and backslashes, exact limits, and rejected over-limit inputs are covered.
`standalone-20260909T073416.970098Z` passed all 24 upstream-tool tests; the
earlier failing receipt remains available. These tests make no network calls.

The integrated profile/process guard suite passed 37 tests at
`standalone-20260909T073714.792255Z`. Fake process-tree tests cover both output
directories, helpers, deleted executables, symlink aliases, other users, vanished
and unreadable entries, a missing process filesystem, and refusal before any
staging/verification work. The guard is a read-only preflight and cannot prevent
a user launching a new browser after it finishes.

Initial 100-URL startup diagnostic passed on 2026-09-09 in
`product-browser-tests-20260909T093527.729468Z` (build `093417.670586Z`).
All pages loaded in 77.9 seconds; native network observation recorded one launch
and no exits. The harness differs from production startup, and debug activation
latency remains open. Initial compile `093306.585602Z` required replacing the
observer-owner field with Chromium `raw_ptr`.

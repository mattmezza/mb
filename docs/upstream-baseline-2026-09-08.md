# Upstream Xorg baseline review

Reviewed 2026-09-08 by Codex using captured images from the actual Arch Linux Xorg session (DISPLAY=:0).

Result: baseline gate passed. This is unmodified Chromium 152.0.7977.82, not the product browser.

- Successful build receipt: `.build/logs/upstream-build-20260908T112027.558220Z.json`; exact pinned Chromium/depot/dependency identities verified by the smoke driver before launch.
- Binary SHA256: `a926c2da717ed645f30b7b23ff134100f5ce699bb838afb153b7279d3a77d3a7`.
- `01-navigation.png`: native browser window and rendered Navigation works heading observed.
- `02-tabs.png`: two titled tabs, active Baseline two and rendered Second tab heading observed. Automated title assertions passed for Alt+Enter new URL tab, previous/next, close and reopen.
- `03-devtools-review-required.png`: Ctrl+Shift+I opened docked Chromium DevTools; Elements displays the inspected page DOM and Second tab h1. Other panels/deeper DevTools functionality remain pending.
- `04-sandbox.png`: browser reports adequate sandboxing, Namespace layer 1, PID/network namespaces, Seccomp-BPF and TSYNC, Yama broker protection. Yama non-broker protection reports No; no sandbox flags were disabled. Live renderer kernel records all show NoNewPrivs=1, Seccomp=2, one filter.
- `05-version.png`: version and exact test launch command observed. Developer build's displayed revision is zero-filled; actual Git pin and binary SHA are established by the build receipt and prelaunch verification. Executable/Profile fields had not populated when captured; /proc and explicit isolated launch root independently verify these.
- Ctrl+Shift+W closed the owned browser normally; recorded exit code 0 and no forced cleanup.

Limitations and prior attempts:

The first attempt (upstream-20260908T180830.166570Z) failed an upstream debug DCHECK in AutocompleteResult::AppendMatches while typing percent-encoded HTML whose decoded suggestion ended in whitespace. Its crash log and fresh test root are retained. The driver now uses base64 HTML test URLs; the upstream defect is not fixed or suppressed. General omnibox regression testing remains required.

The second attempt (upstream-20260908T180936.624761Z) passed automation but its F12 screenshot after two seconds did not show DevTools. The final attempt uses Ctrl+Shift+I and ten seconds for first frontend load. F12 remains unverified, not reported broken or passed.

Ctrl+T is deferred until a bundled local product new-tab page exists: the stock regular NTP can fetch remote executable OneGoogleBar UI. Baseline uses Alt+Enter to open the supplied local data URL directly in a new tab. No network, certificate, origin, site-isolation or sandbox protections were disabled. Browser logs include upstream debug warnings and shutdown WebFrame leak diagnostics; normal process exit passed but these observations are not a claim of full capability certification.

# Product branding runtime review — 2026-09-08

Passed Phase 2 branding launch scope after reviewing actual Xorg screenshots.
The executable and WM_CLASS match the manifest (mb / com.matteo.mb).
Navigation rendered, the two-tab view selected the second tab, and shortcut
selection/close/reopen assertions passed. Docked DevTools Elements displayed
the second tab DOM. This is not verification of every DevTools panel or F12.

chrome://sandbox displayed namespace isolation, PID/network namespaces,
Seccomp-BPF with TSYNC, Yama broker protection, and “adequately sandboxed”.
All 11 recorded renderers had NoNewPrivs=1, Seccomp=2 and a filter. Non-broker
Yama protection was No, matching the upstream baseline. No security-disabling
launch flags were used. The owned browser exited via its UI with code 0.

chrome://version displayed mb 152.0.7977.82 and the original generated temporary
icon. Chromium Authors attribution remains. The development revision is zero
filled and executable/profile fields were not populated at capture; the exact
pinned Git revision, binary hash and running executable/profile are independently
recorded in the build/integration/smoke receipts.

Earlier product-20260908T185922.785254Z timed out navigating to Sandbox after
DevTools toggle. A diagnostic capture added a short settling interval; two
subsequent runs passed. The exact timing cause remains unproven, and the earlier
failure is retained. A post-toggle screenshot must not be treated as proof of
DevTools closure. Future browser tests should synchronize actual DevTools state.

The intermediate passing run product-20260908T190651.858141Z exposed remaining
Chromium version artwork; the final component resources now derive from the
same original manifest asset at 100/200/300 percent scales.

This passes the renamed debug browser milestone only. The horizontal tab strip
is still present; configuration is not consumed by browser startup. Vertical UI,
environments, incognito persistence, extension compatibility, broad browser and
X11 capabilities, optimized release and Arch packaging remain pending.

Evidence: `.build/test-evidence/product-20260908T191032.883440Z/`.
Build: `.build/logs/product-build-20260908T190956.375119Z.json`.
Core tests: `.build/test-evidence/product-unit-20260908T184731.229729Z/`.

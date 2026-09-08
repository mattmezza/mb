# Changelog

## Unreleased

- Record Arch/Xorg discovery and phased implementation gates.
- Pin Chromium Linux stable 152.0.7977.82 and depot_tools by commit.
- Add dependency preflight and guarded source-fetch tooling.
- Verify installed Arch dependencies and fetch the pinned upstream checkout.
- Add logged upstream hook/generation/build stages and a scoped X11 test protocol.
- Complete pinned dependency sync, hooks, GN generation and unmodified upstream build.
- Add centralized branding and tested C++/GN/resource/desktop metadata generation.
- Prepare a build-receipt-bound X11 smoke driver using a fresh isolated test root.
- Add a compiled, tested argument normalizer for the future early startup hook.
- Add strict versioned TOML parsing, bounded file loading, and a compiled
  configuration-validation/environment-inspection companion command.
- Add secure environment directory resolution, auditing, and preparation with
  no-follow traversal, private permissions, collision and path-length checks.
- Bound parser table/value depth and add malformed-input regression coverage.
- Prepare a local Manifest V3 capability fixture; runtime checks remain pending.
- Add loopback pages for storage-isolation, download, audio and permission checks.
- Generate original native icon assets and pinned GRIT strings while preserving
  resource IDs, translations and attribution. Reserve the custom URL scheme
  until a browser handler exists.
- Compose a runtime configuration snapshot with explicit/remembered/default
  selection, all-environment auditing and no directory creation.
- Add a logged focused-test runner; all 107 standalone tests pass using the
  pinned Python interpreter, including fresh product C++ builds.
- Pass the actual Xorg upstream baseline with reviewed rendering, tabs, docked
  DevTools, namespace/Seccomp sandbox and clean shutdown. Preserve the first
  data-URL debug assertion failure in the dated review; begin Phase 2 integration.
- Add a read-only stable-release candidate checker and document the upstream
  update procedure, packaging payload requirements and real-credits build gate.

- Pass the branded debug browser Xorg smoke with manifest window identity, original
  version artwork, native sandbox evidence and clean shutdown; retain prior failures.
- Add a focused native browser-test executable and verify authoritative tab operations.
- Bundle the local manifest-branded new-tab page, preserve native incognito and
  extension override routing, and pass seven browser tests plus reviewed Ctrl+T/Xorg smoke.

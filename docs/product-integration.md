# Product integration

Phase 1 passed on 2026-09-08; the [baseline review](upstream-baseline-2026-09-08.md)
records its scope. Product generation, browser compilation, all 51 C++ tests and
17 companion checks passed. The renamed browser also passed its
[scoped Xorg launch](product-baseline-2026-09-08.md), including manifest identity,
sandbox and clean exit. The initial timeout and subsequent version-page artwork
correction are retained in that review.

Run from the repository root after completing the pinned upstream baseline:

```sh
python3 mb/tools/product.py prepare --baseline-review .build/test-evidence/upstream-20260908T181043.265925Z/review.json
python3 mb/tools/product.py gen
python3 mb/tools/product.py test --jobs 12
python3 mb/tools/product.py build --jobs 12
```

The final branding build receipt is
`.build/logs/product-build-20260908T190956.375119Z.json`; reviewed product evidence
is `.build/test-evidence/product-20260908T191032.883440Z/review.json`.

Replace the review path with the actual passed review on a new machine. The first
preparation checks the review text and screenshot hashes, successful test/build
receipts, pinned revisions and the exact original binary. Preserve that evidence.
The review must be written after visual inspection; it is not an automated claim
that a screenshot filename proves correct rendering. The smoke driver emits raw
results; the reviewed `review.json` records schema_version 1, result passed,
evidence_directory, build_receipt, binary_sha256, review_sha256 (results.md), and
screenshots (filename-to-SHA256 map), as exemplified in the retained baseline.

Later changes use `python3 mb/tools/product.py prepare` followed by `gen` and the
appropriate build. Preparation regenerates branding from the authoritative
manifest, derives native strings from pinned pristine GRD/translation inputs,
rasterizes the original SVG, and copies the product layer into `//mb`. Generated
native icons occupy Chromium's existing scale-specific theme layout. Original
monochrome product and password-manager key assets live under `mb/resources`;
preparation stages both vector icon names and both favicon scales. These are
explicitly temporary, replaceable artwork. Product
scripts are operated from this repository, not their staged source-tree copies.

Each patch under `mb/patches/` is applied in filename order to pristine pinned
blobs in an isolated scratch repository. The resulting bytes define the expected
Chromium changes. Existing destination content must match pristine source, the
previous integration receipt, or the intended result. Unrecognized edits,
unrelated source changes and symlink destinations fail before staging. Owned,
unchanged outputs retired by regeneration move to a dated `.build/integration-retired`
archive. No user profile or unrelated repository file is removed.

The integration receipt records every staged file hash, patch hash, manifest
identity, baseline gate and pinned Git dependencies. `product.py verify` checks
those files and current product source. GN/build steps verify before execution,
write timestamped logs and JSON receipts, and retain a 25 GiB working reserve.
A successful browser build records the renamed ELF hash. Product GN arguments
are checked in; switching from the exact baseline arguments is allowed once.
Unexpected local GN arguments are preserved and rejected.

The initial patch changes twelve upstream browser/build/resource files; a second
patch redirects three component version-page artwork resources to generated assets:

- `chrome/BUILD.gn`: Linux executable output name, preserving the `chrome` GN label.
- `chrome/app/BUILD.gn`, `components/strings/BUILD.gn`: generated native strings and aliases to Chromium’s resolved resource-ID allocation.
- `chrome/app/theme/chrome_unscaled_resources.grd`: shortcut icons use the selected product artwork.
- `chrome/common/{BUILD.gn,channel_info_posix.cc,chrome_constants.cc,chrome_paths_linux.cc}`: generated header dependencies, desktop filename, executable and profile paths.
- `chrome/browser/{BUILD.gn,shell_integration_linux.cc,shell_integration_linux.h}`: generated identity for icons, WM_CLASS and direct-launch scheme; desktop-basename helper for regular Wayland windows.
- `chrome/browser/ui/views/frame/browser_native_widget_aura_linux.cc`: regular Wayland app ID follows the desktop basename; app/PWA handling is preserved. Wayland is not certified.

No renderer, sandbox, network, site-isolation or engine source is patched. Internal
resource filenames and protocol/engine version identity remain Chromium's. The
custom URL scheme is derived in Chromium but remains absent from desktop MIME
registration until its invocation behavior is tested. The stock development
wrapper and final packaging launcher still need explicit product handling.

`product-debug.gn` enables real credits, unbranded resources and Ozone X11.
The separate `product-release.gn` profile is now implemented; select it with
`--profile release` for generation/build/test stages. Its optimized browser and
Arch package are not yet built or tested. Profile-bound receipts and commands
are described in [Arch build setup](build-arch-linux.md#product-debug-and-release-profiles).
Staging and build stages refuse to proceed while identifiable same-user
processes execute under either product output; close trial windows before builds.
The renamed browser must pass its own scoped runtime checks; successful GN
generation or standalone tests do not establish that milestone.

The `test` stage builds the focused GN test and companion targets, creates
private test scratch directories, sets the fixture-specific environment paths,
and records both the Chromium unit launcher and companion end-to-end results.
`test-build` compiles without running. Raw `mb_unit_tests` needs
`MB_ENVIRONMENT_TEST_TMPDIR` and `MB_RUNTIME_CONFIG_TEST_TMPDIR` pointing to
owned scratch storage; use the wrapper for reproducible setup.

The first full locale repack exposed a static resource-ID seed collision. Product
GN now derives aliases from Chromium's generated `default_resource_ids`; it does
not ship the standalone seed-based pak allocation. The original allocation and
repack collision checks remain active. Real credits generated successfully with
toml++ included; packaging must preserve those notices.

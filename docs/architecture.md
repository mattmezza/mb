# Architecture and integration decisions

Status: the unmodified upstream build and sandboxed Xorg baseline passed.
The product core and companion compile and pass tests inside Chromium GN.
Native branding integration is applied and the renamed browser is compiling.
The custom browser UI and startup service wiring remain pending.

## Product boundary

The intended dependency direction is native product UI → product controllers →
narrow Chromium integration → existing browser services. Blink, V8, networking,
sandboxing and site isolation retain their upstream implementations.

The product layer will live under Chromium's `//mb/` and expose windows, tab
commands, configuration, environments, sidebar state and environment activation.
`Browser`, `TabStripModel`, `Profile` and `WebContents` remain authoritative.
Controllers may project their state into Views; they must not maintain a second
independent tab collection or implement a new profile/session engine.

The setup repository and downloaded source checkout are separate Git
repositories. The latter is a direct pinned Chromium checkout, not a browser
embedding framework. The implemented `product.py` workflow stages the product overlay, checks
reviewed patches against pristine pinned blobs, preserves local edits, and
records reproducible GN/build/test receipts. See [integration](product-integration.md).

## Native vertical tabs in the pinned release

Source inspection found an existing Views vertical-tab implementation. The
integration should reuse its tab projection, drag controller, pinned/group
views, accessibility and resize behavior, while adding explicit product-owned
composition and command routing. Enabling an upstream preference alone is not
the planned product implementation.

The following paths are relative to the pinned Chromium source:

| Integration point | Existing responsibility and constraint |
| --- | --- |
| `chrome/browser/ui/views/frame/browser_view.cc` | Creates the vertical region and switches the active strip with reset/initialize operations; retain the working toolbar and omnibox. |
| `chrome/browser/ui/views/frame/base_tab_strip_region_view.cc` | Constructs the native root tab projection, collection controller and drag handler from the browser's tab model. |
| `chrome/browser/ui/views/frame/vertical_tab_strip_region_view.{h,cc}` | Final native host with resize and state delegates; preserve its frame/layout contracts when introducing product composition. |
| `chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.{h,cc}` | Routes selection, close, reordering, grouping and context menus to authoritative state. |
| `chrome/browser/ui/tabs/vertical_tab_strip_state_controller.cc` | Persists expanded width and collapsed state using profile preferences and session window data. |
| `chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc` | Allocates native sidebar space and assumes specific corners, caption exclusions and animation behavior. |

There must be exactly one active native tab projection.
`RootTabCollectionNode::Init()` registers itself with
`TabStripModel::SetTabStripUI()`. Mounting another native root alongside it can
violate that contract. Product composition must preserve the existing
reset/initialize lifecycle.

The first intended seam is product-owned sidebar controls within the existing
host, keeping its native tab subtree and frame integration. Current top/footer
containers also provide tab-search anchors and drag-bound calculations. Any
replacement must preserve or explicitly adapt those contracts. The concrete
API and narrow patch list must be reviewed against compilation in Phase 3.

The native implementation has scrolling but eagerly creates child tab views.
Its responsiveness with 100 tabs remains untested. Existing upstream tests are
useful starting points, not acceptance evidence for this fork.

## Environments and private browsing

Each named environment must select a separate Chromium user-data root before
profile and process-singleton initialization. A switch can start or activate a
different process. An environment is not a profile inside a shared data root.
Chromium's singleton and off-the-record profile behavior must be preserved;
product code must not replace either with ad hoc locks or history deletion.

Source inspection identified these startup seams, still awaiting integration:

- Normalize original arguments in `chrome/app/chrome_main.cc` before
  its existing `CommandLine::Init()` call, using the string-vector overload.
  It copies the normalized strings. Keep `ContentMainParams.argc/argv` and their
  original bytes unchanged: Linux process-title initialization expects the real
  contiguous argv/environment memory, not heap-owned replacement strings.
  ContentMain's later command-line initialization is a no-op; process dispatch
  uses the global normalized copy. Paired selectors cannot reliably be recovered
  after Chromium parses argv. Do not reset an already initialized command line
  in test/embedding entry paths or mutate the environment during normalization.
- Resolve configuration and the selected root in
  `ChromeMainDelegate::BasicStartupComplete()`, for the browser process only,
  after its remote-debugging-pipe descriptor validation and before profile
  initialization. Preserve the existing security guards and child-process path.
- Set the authoritative `--user-data-dir` before `PreSandboxStartup()` calls
  `InitializeUserDataDir()`. Singleton creation follows in
  `PostEarlyInitialization()`. Browser-main-parts hooks are too late for this.

The standalone runtime snapshot supplies validated configuration and selection;
it does not create directories or substitute for the native singleton. Browser
integration must separately validate startup URLs with Chromium's URL parser,
prepare only the selected root, and retain genuine `--incognito` behavior.

## Configuration and identity

One versioned branding manifest supplies generated product identity inputs.
A separate strict, versioned TOML parser and directory core are implemented and
tested in isolation, with a compiled control command for validation and path
inspection. Their browser integration is pending. Native sidebar persistence
already exists. The selected precedence policy keeps TOML read-only: changed
configured width/collapse values override startup state, while unchanged values
permit native saved window state and interactive resizing to survive restarts.

The initial-state seam is immediately before `VerticalTabStripStateController`
construction in `BrowserWindowFeatures::Init()`. Native session restoration
supplies per-window width/collapse; ordinary new windows can fall back to profile
preferences. Controller changes persist both kinds of state. The later embedder
features hook runs too late to substitute constructor state, and collapse
requests before the View installs its delegate do nothing.

Product profile markers will compare configured width and collapse separately,
once per original profile per process. Changed fields must apply to every window
in the startup restore batch, then yield to native persistence. A session-restored
callback plus an explicit no-restore startup completion path can end that batch.
Subsequent manual reopening retains its saved window state. Marker/session writes
are asynchronous and must not be described as an atomic transaction. Incognito
window state must not be written to the normal session store.

Close-button visibility is centralized in native `TabView::IsChildVisible()`;
there is no existing preference for the requested switch. Product integration
will preserve the native close callback and conditional visibility when enabled,
and suppress the button when disabled. Browser light/dark/system selection can
use `ThemeService::SetBrowserColorScheme()` rather than writing its preference
directly; the service also handles Linux theme-provider changes. These policies
and hooks have not yet been compiled into the browser.

## Evidence

All upstream observations above refer to
[the pinned Chromium commit](https://chromium.googlesource.com/chromium/src/+/d04cdb24d67b081f6cf80200ffc5233f44b61109/).
The binding build/test gate is recorded in [STATUS.md](../STATUS.md), and
[testing.md](testing.md) distinguishes procedures from observed results.

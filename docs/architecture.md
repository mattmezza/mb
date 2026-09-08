# Architecture and integration decisions

Status: source inspection only. Product implementation is gated on the
unmodified upstream build and sandboxed X11 launch. The current repository
contains setup tooling; it does not yet contain a custom browser UI.

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
embedding framework. A reproducible product-overlay/patch workflow will be
implemented after the upstream gate; its exact mechanics are still pending.

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

## Configuration and identity

One versioned branding manifest will supply all product identity. A separate
strict, versioned TOML user configuration will control product preferences and
environment roots. Both are pending implementation. Native sidebar persistence
already exists; configuration precedence and runtime persistence must be
specified together to avoid two competing stores or overwriting user edits.

## Evidence

All upstream observations above refer to
[the pinned Chromium commit](https://chromium.googlesource.com/chromium/src/+/d04cdb24d67b081f6cf80200ffc5233f44b61109/).
The binding build/test gate is recorded in [STATUS.md](../STATUS.md), and
[testing.md](testing.md) distinguishes procedures from observed results.

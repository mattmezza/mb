# Local product new-tab page

This change supplies a small, bundled mb page for regular browser windows.
Its heading and title come from the generated branding manifest. Its only
resources are compiled HTML and CSS, served through `WebUIDataSource`'s request
filter. Unknown resource paths fail. There are no scripts, frames, Mojo bindings,
stock NTP handlers, image proxies, or remote resource URLs. CSP prohibits scripts,
connections, frames, workers, objects and form submissions and permits only
same-origin stylesheets. Existing Trusted Types and frame-embedding protections
remain enabled.

The page retains Chromium's `chrome://new-tab-page/` origin. Direct navigation
to `chrome://new-tab-page-third-party/` also uses the product controller. The
controller is independent of upstream `NewTabPageUI`; constructing the upstream
class would create services even if its visible page were replaced.

Linux new-tab URL selection always returns the local product origin for regular
profiles. This does not change the configured search engine or omnibox searches.
The extension URL override handler still runs before built-in search rewriting,
so installed extensions can supply their own new-tab pages using Chromium's
normal extension rules. Extension code remains governed by extension security
policy; this change concerns the built-in product page.

Incognito and guest routing remain upstream behavior. The product configs reject
off-the-record contexts, allowing the existing `chrome://newtab/` controller to
display Chromium's privacy explanation. The original non-OTR guest fallback is
also preserved. Chromium already prohibits extension new-tab overrides in
incognito.

## Managed policy

On Linux, `NewTabPageLocation` is deliberately not used for NTP URL rewriting.
Its handler otherwise runs before both extension overrides and built-in URL
selection, allowing a managed remote page to bypass the local-only requirement.
The setting may still be displayed by `chrome://policy`; this patch does not
remove the policy schema or add a policy-error diagnostic. Administrators should
not rely on this setting in mb. The URL remains accessible through ordinary
navigation, subject to Chromium's usual browsing policies.

## Integration and remaining validation

`0003-local-new-tab.patch` changes Linux WebUI registrations, URL selection and
the managed-policy rewrite. It adds the product controller sources directly to
`//chrome/browser/ui/webui:configs`, with explicit dependencies on the generated
branding header and the upstream APIs used. This avoids a dependency cycle back
from the root product core to browser aggregation. The page needs no new GRIT
allocation or resource pak.

Six focused browser tests cover the three built-in URL entry points, the remote
NTP preference guard, unchanged selection of a search provider advertising a
remote NTP, an unpacked MV3 new-tab override and disable/restore, CSP refusal of
a data-module import, and genuine native incognito routing. The preference test
writes the underlying preference; it does not simulate a managed policy provider.
All six NTP tests and the tab baseline passed in 76 seconds on actual X11:
`.build/test-evidence/product-browser-tests-20260908T201439.291090Z/results.json`.
GN header dependency checks also passed. Guest windows and absence of remote
page-resource requests still need runtime checks. A page-resource guarantee
does not imply that the whole browser makes no background network requests.

The production Ctrl+T/Xorg gate also passed with direct screenshot review:
`.build/test-evidence/product-20260908T201725.564345Z/review.json`, built from
`.build/logs/product-build-20260908T201640.499308Z.json`. The page visibly renders
the manifest heading and address/search hint; native tabs, docked DevTools,
sandbox diagnostics and clean shutdown passed their scoped checks. Reproduce
with `product_smoke.py --build-receipt PATH --local-ntp`. The early navigation
screenshot preceded painting; later page screenshots establish rendering.

Origin-based consumers such as bookmarks-bar handling and reverse URL rewriting
remain applicable. Upstream Customize Chrome controls can still appear because
they identify the NTP by origin; their background/module settings do not control
this static mb page. Product markup does not request the stock NTP Mojo
interfaces or embed Microsoft's authentication iframe, which requires an actual
upstream `NewTabPageUI` controller.

Source seams in Chromium 152.0.7977.82:

- `chrome/browser/ui/webui/chrome_web_ui_configs.cc`: regular and third-party
  NTP config registration.
- `chrome/browser/search/search.cc`, `NewTabURLDetails::ForProfile`: OTR guard,
  provider selection, and the shared URL used for NTP recognition.
- `chrome/browser/chrome_content_browser_client.cc`,
  `HandleNewTabPageLocationOverride` and `BrowserURLHandlerCreated`: managed
  policy before extensions before built-in search rewriting.
- `chrome/browser/extensions/extension_url_overrides.cc`: normal overrides and
  the incognito new-tab exclusion.
- `chrome/browser/ui/webui/ntp/new_tab_ui.cc`: native incognito and guest page.
- `content/browser/webui/web_ui_data_source_impl.cc`: request filters run before
  other data-source resources; CSS requests receive the CSS MIME type.

The unchanged upstream omnibox currently shows an AI Mode affordance on NTP.
No product AI functionality was added. Removing that upstream affordance and
irrelevant Customize Chrome controls remains UI cleanup after sidebar stability.

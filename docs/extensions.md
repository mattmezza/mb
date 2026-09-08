# Extension and Google-service limits

This project is an unbranded upstream Chromium build. It must not ship, copy,
or suggest unofficial Google API keys. The branded debug browser has launched
under Xorg. A focused browser test loads a local unpacked MV3 NTP extension,
checks that its page overrides chrome://newtab, disables it through the native
registrar and verifies restoration of the bundled page. Evidence:
`.build/test-evidence/product-browser-tests-20260908T201439.291090Z/results.json`.
This does not establish service-worker, toolbar-action, storage, incognito or
cross-environment compatibility; those checks remain **pending**.

## What the source establishes

- Without Chrome branding or the Google-internal keys file, Chromium leaves
  its Google API key and OAuth defaults unset. The source also permits
  environment overrides in an unbranded build, but that is not a distribution
  mechanism. We will not configure either personal or unofficial keys.
- Google documents that Chromium sign-in is restricted and that private Chrome
  APIs, including Chrome Sync, are restricted for third-party Chromium
  browsers. Treat Chrome Sync and browser sign-in as unavailable for this
  product; do not infer this from, or conflate it with, Web Store behavior.
- Chromium contains the normal Chrome Web Store installer and its production
  update URL path. Its item-metadata fetcher says that its endpoint does not
  require an API key and only attaches one for a Chrome-branded build. This is
  evidence of an unbranded request path, **not** evidence that the service will
  accept this binary, every extension, or every install/update flow.
- The pinned build exposes `webstorePrivate` to the current storefront origin,
  `https://chromewebstore.google.com/`, without a Google-branding gate. The
  two-stage install flow requires a prior approval for the same browser context
  and extension ID; completing installation rejects guest/incognito contexts.
  Begin store tests in a regular environment window and test incognito access
  separately after installation.
- Unbranded extension requests use the upstream `chromiumcrx` protocol identity.
  The inspected install entry points do not gate installation on the executable
  filename. Keep protocol identity separate from replaceable display branding.
  This does not establish how the remote storefront treats browser identity.
- Store installs retain CRX3 publisher-proof verification. Linux manual packed
  installs retain signed CRX3, ID, manifest, prompt and policy checks. A deliberate
  dropped-file installation on `chrome://extensions` uses a different permitted
  path from an ordinary off-store download. Do not treat every downloaded CRX
  as automatically installable or change verification to make it install.

## Pending runtime checks

- Chrome Web Store listing, install, update, removal, and error behavior.
- Manifest V3 service workers, permissions prompts, declarative APIs, and
  extension-specific compatibility.
- Incognito access and profile separation for installed extensions.
- Local unpacked/CRX and policy-managed installation paths.

No compatibility claim is made until these checks run against the built
binary. If Chrome Web Store installation is unavailable, use only legitimate
developer, enterprise-policy, or publisher-supported distribution methods;
do not bypass store controls.

First load the owned unpacked MV3 fixture through Developer mode and Load
unpacked in an isolated regular environment. Then test a public store listing
without origin overrides or other installation switches. Record listing/button
availability, permission approval, download response/error, installed ID/version,
restart persistence and removal separately. Successful installation does not
establish that extension updates work; updates need their own observation.

## Sources

- Pinned Chromium `d04cdb24d67b081f6cf80200ffc5233f44b61109`:
  `google_apis/config.gni`, `google_apis/default_api_keys-inc.cc`,
  `extensions/browser/webstore_installer.cc`, and
  `extensions/browser/webstore_data_fetcher.cc`.
- Additional pinned install paths: `extensions/common/api/_api_features.json`,
  `extensions/browser/api/webstore_private/webstore_private_api.cc`,
  `components/update_client/update_query_params.cc`,
  `extensions/common/verifier_formats.cc`, `extensions/browser/crx_installer.cc`,
  and `chrome/browser/extensions/api/developer_private/developer_private_functions.cc`.
- [Chromium API keys documentation](https://www.chromium.org/developers/how-tos/api-keys/) documents key ownership, restricted Chromium sign-in, and the restricted `chrome.identity.getAuthToken` implementation.
- [Chromium announcement on private API availability](https://blog.chromium.org/2021/01/limiting-private-api-availability-in.html) covers the Chrome Sync/private-API restriction.
- [Chrome extension installation documentation](https://developer.chrome.com/docs/extensions/how-to/distribute/install-extensions) describes supported installation methods; it does not certify an unbranded build's Web Store access.
- [Linux self-hosting](https://developer.chrome.com/docs/extensions/how-to/distribute/host-on-linux)
  describes publisher packaging and update distribution. Its older examples do
  not replace verification of the pinned browser's current install paths.
- [Manifest V3 Hello World](https://developer.chrome.com/docs/extensions/get-started/tutorial/hello-world)
  documents the Developer mode/Load unpacked workflow used by the local fixture.

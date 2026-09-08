# Extension and Google-service limits

This project is an unbranded upstream Chromium build. It must not ship, copy,
or suggest unofficial Google API keys. The binary has not launched yet, so items
marked **pending** require a browser-level check.

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

## Sources

- Pinned Chromium `d04cdb24d67b081f6cf80200ffc5233f44b61109`:
  `google_apis/config.gni`, `google_apis/default_api_keys-inc.cc`,
  `extensions/browser/webstore_installer.cc`, and
  `extensions/browser/webstore_data_fetcher.cc`.
- [Chromium API keys documentation](https://www.chromium.org/developers/how-tos/api-keys/) documents key ownership, restricted Chromium sign-in, and the restricted `chrome.identity.getAuthToken` implementation.
- [Chromium announcement on private API availability](https://blog.chromium.org/2021/01/limiting-private-api-availability-in.html) covers the Chrome Sync/private-API restriction.
- [Chrome extension installation documentation](https://developer.chrome.com/docs/extensions/how-to/distribute/install-extensions) describes supported installation methods; it does not certify an unbranded build's Web Store access.

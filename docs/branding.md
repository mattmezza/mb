# Branding generator

[`mb/branding.toml`](../mb/branding.toml) is the single authoritative temporary
product-identity manifest. It uses schema version 1 and currently defines `mb`,
`Matteo's Browser`, and `com.matteo.mb`.

Generate isolated future integration inputs with Python 3.11 or newer:

```sh
python3 mb/tools/branding.py
```

The default destination is `.build/generated/branding`; use `--out-dir PATH` to
select another destination. The generator refuses a nonempty unmanaged output
directory and always refuses paths in `.build/chromium/src`. Its generated files
are a C++20 header, GN values, a Chromium-format `BRANDING` file, GRIT-part XML
strings, a desktop entry, packaging JSON, and an original abstract SVG. The
`BRANDING` file supplies every key consumed by Chromium's legacy
`build/util/branding.gni` and `chrome/common/chrome_version.h.in`: temporary
company labels derive from the product full/short names, installer labels append
`Installer`, `MAC_BUNDLE_ID` derives from `application_id`, and the unsupported
Mac creator/team fields are intentionally empty. This does not claim Mac bundle
or signing support.

The manifest validates exact keys and types,
rejects control characters and unsafe identifier/file values, and creates the
desktop entry's quoted executable with `%U`, plus HTML, XHTML, HTTP and HTTPS
handler metadata. The manifest's custom URL scheme is reserved and exported in
generated values; the desktop entry does not register it before a corresponding
browser protocol handler exists. GN output escapes literal dollar signs so manifest
text cannot interpolate GN variables. Legacy `BRANDING` values feed raw GN scope
substitution and quoted C++ macros, so `product.full_name` rejects double quotes,
backslashes, dollar signs, and `@` rather than attempting unsafe escaping. Every
manifest validation diagnostic names both the manifest path and the rejected key.

`branding_strings.grdp` remains auxiliary future-GRIT input. It is not a
replacement for the actual `IDS_PRODUCT_NAME` resource wiring, which remains
pending a separate generated-GRD integration.

This tooling does not patch Chromium, configure a browser target, rename an
executable, or establish that a Chromium build has passed. It is intentionally
separate until the unmodified Chromium baseline build and test gate succeeds.

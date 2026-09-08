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
are a C++20 header, GN values, GRIT-part XML strings, a desktop entry, packaging
JSON, and an original abstract SVG. The manifest validates exact keys and types,
rejects control characters and unsafe identifier/file values, and creates the
desktop entry's quoted executable with `%U`, plus HTML, XHTML, HTTP, HTTPS, and
custom scheme-handler metadata. GN output escapes literal dollar signs so manifest
text cannot interpolate GN variables. Every manifest validation diagnostic names
both the manifest path and the rejected key.

This tooling does not patch Chromium, configure a browser target, rename an
executable, or establish that a Chromium build has passed. It is intentionally
separate until the unmodified Chromium baseline build and test gate succeeds.

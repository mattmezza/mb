# Branding asset preparation

`mb/tools/branding_assets.py` creates isolated native asset variants from the
original SVG produced by `branding.generate()`. It uses the installed
`rsvg-convert` renderer and no fonts, remote assets, image generation, or
Chromium artwork:

```sh
python3 mb/tools/branding_assets.py
```

The default destination is `.build/generated/branding-assets`. It contains
generic `unscaled/`, `default_100_percent/`, and `default_200_percent/` trees,
plus `receipt.json`. The receipt records the SVG SHA-256, complete renderer
version output including linked libraries, and every PNG SHA-256. Re-running on
the same renderer environment is expected to reproduce identical files.

The source inventory was checked against Chromium's
`chrome/app/theme/chrome_unscaled_resources.grd` and `theme_resources.grd`.
The Linux inventory actively references the 64/128/256 unscaled logos and the
16 scaled logo; this generator also prepares the requested 24/48 Linux variants
for future staging. No files are copied into Chromium. A later integration may
stage the outputs under a Chrome theme component named `mb`.

`product_logo_name_22.png` and its white counterpart are temporary square
icon-only 22/44-pixel placeholders. They deliberately avoid wordmark text and
font dependencies and can be replaced when the final branding is chosen.
`product_logo_animation.svg` is likewise the same static original SVG until an
original animation is designed.

The generator refuses nonempty unmanaged output directories and unsafe managed
paths, including symlinked output parents. It does not integrate, patch, or
validate runtime Chromium resources.

Temporary rendering files use `.build/tmp`. The renderer is provided by the
official Arch `librsvg` package; version `2:2.62.3-1` was already installed on
the test machine. `rsvg-convert --version` reported 2.62.3, Cairo 1.18.4,
Pango 1.58.2, HarfBuzz 14.4.0, and Fontconfig 2.18.3. Four renderer-backed tests
passed via `python3 -m unittest mb.test.test_branding_assets -v`; the 128px PNG
was also visually inspected as an original, replaceable placeholder.

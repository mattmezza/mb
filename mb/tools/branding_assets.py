#!/usr/bin/env python3
"""Rasterize the generated temporary branding SVG into native asset variants."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import branding


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / ".build" / "generated" / "branding-assets"
MANAGED_FILE = ".branding-assets-managed.json"
MANAGED_SCHEMA_VERSION = 1
UNSCALED_SIZES = (16, 24, 48, 64, 128, 256)
LINUX_UNSCALED_SIZES = (24, 48, 64, 128, 256)
SCALED_ASSETS = {
    "product_logo_32.png": 32,
    "linux/product_logo_16.png": 16,
    "product_logo_name_22.png": 22,
    "product_logo_name_22_white.png": 22,
}


class BrandingAssetsError(ValueError):
    """An asset-input, renderer, or managed-output error."""


def sha256_bytes(contents):
    return hashlib.sha256(contents).hexdigest()


def asset_paths():
    paths = [f"unscaled/product_logo_{size}.png" for size in UNSCALED_SIZES]
    paths += [f"unscaled/linux/product_logo_{size}.png" for size in LINUX_UNSCALED_SIZES]
    paths += ["unscaled/product_logo.svg", "unscaled/product_logo_animation.svg"]
    for scale, multiplier in (("default_100_percent", 1), ("default_200_percent", 2)):
        paths += [f"{scale}/{name}" for name in SCALED_ASSETS]
    return tuple(paths)


def planned_paths():
    return asset_paths() + ("receipt.json",)


def find_renderer():
    renderer = shutil.which("rsvg-convert")
    if not renderer:
        raise BrandingAssetsError("rsvg-convert is required to generate branding assets")
    try:
        version = subprocess.run(
            [renderer, "--version"], check=True, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        ).stdout
    except subprocess.CalledProcessError as error:
        raise BrandingAssetsError(f"could not read rsvg-convert version: {error}") from error
    return renderer, version


def assert_safe_output_directory(output_dir):
    if output_dir.is_symlink():
        raise BrandingAssetsError(f"out-dir: refusing symlinked output directory: {output_dir}")
    resolved = output_dir.resolve()
    chromium = branding.CHROMIUM_SOURCE.resolve()
    if resolved == chromium or chromium in resolved.parents:
        raise BrandingAssetsError("out-dir: refusing to generate inside .build/chromium/src")
    return resolved


def check_managed_directory(output_dir, paths):
    if not output_dir.exists():
        return
    if not output_dir.is_dir():
        raise BrandingAssetsError(f"out-dir: not a directory: {output_dir}")
    contents = list(output_dir.iterdir())
    if not contents:
        return
    marker = output_dir / MANAGED_FILE
    if marker.is_symlink() or not marker.is_file():
        raise BrandingAssetsError(f"out-dir: refusing nonempty unmanaged directory: {output_dir}")
    try:
        managed = json.loads(marker.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BrandingAssetsError(f"out-dir: invalid managed-output marker: {error}") from error
    if managed != {"schema_version": MANAGED_SCHEMA_VERSION, "outputs": sorted(paths)}:
        raise BrandingAssetsError("out-dir: managed-output marker does not match this generator")
    for relative in paths:
        parent = output_dir
        for part in Path(relative).parts[:-1]:
            parent = parent / part
            if parent.is_symlink() or (parent.exists() and not parent.is_dir()):
                raise BrandingAssetsError(f"out-dir: refusing unsafe output parent: {parent}")
        destination = output_dir / relative
        if destination.is_symlink() or (destination.exists() and not destination.is_file()):
            raise BrandingAssetsError(f"out-dir: refusing unsafe output path: {destination}")


def atomic_write(path, contents):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(contents)
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def rasterize(renderer, source, width, height, destination):
    subprocess.run(
        [renderer, "--width", str(width), "--height", str(height), "--output", str(destination), str(source)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def generate(manifest_path=branding.DEFAULT_MANIFEST, output_dir=DEFAULT_OUTPUT):
    """Generate assets using branding.generate() as the SVG source of truth."""
    output_dir = assert_safe_output_directory(Path(output_dir))
    paths = planned_paths()
    check_managed_directory(output_dir, paths)
    renderer, renderer_version = find_renderer()
    scratch = ROOT / ".build" / "tmp"
    scratch.mkdir(mode=0o700, parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="branding-assets-", dir=scratch) as temporary:
        temporary = Path(temporary)
        branding_dir = temporary / "branding"
        branding.generate(Path(manifest_path), branding_dir)
        source_svg = branding_dir / "icon.svg"
        source_bytes = source_svg.read_bytes()
        rendered = temporary / "rendered"
        rendered.mkdir()
        outputs = {
            "unscaled/product_logo.svg": source_bytes,
            "unscaled/product_logo_animation.svg": source_bytes,
        }
        for size in UNSCALED_SIZES:
            relative = f"unscaled/product_logo_{size}.png"
            target = rendered / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            rasterize(renderer, source_svg, size, size, target)
            outputs[relative] = target.read_bytes()
        for size in LINUX_UNSCALED_SIZES:
            relative = f"unscaled/linux/product_logo_{size}.png"
            target = rendered / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            rasterize(renderer, source_svg, size, size, target)
            outputs[relative] = target.read_bytes()
        for scale, multiplier in (("default_100_percent", 1), ("default_200_percent", 2)):
            for name, size in SCALED_ASSETS.items():
                relative = f"{scale}/{name}"
                target = rendered / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                rasterize(renderer, source_svg, size * multiplier, size * multiplier, target)
                outputs[relative] = target.read_bytes()
        receipt = {
            "schema_version": 1,
            "source_svg_sha256": sha256_bytes(source_bytes),
            "renderer": {"command": "rsvg-convert --version", "version_output": renderer_version},
            "png_sha256": {path: sha256_bytes(outputs[path]) for path in sorted(outputs) if path.endswith(".png")},
        }
        receipt_bytes = (json.dumps(receipt, indent=2, sort_keys=True) + "\n").encode("utf-8")

    output_dir.mkdir(parents=True, exist_ok=True)
    for relative in sorted(outputs):
        atomic_write(output_dir / relative, outputs[relative])
    atomic_write(output_dir / "receipt.json", receipt_bytes)
    atomic_write(
        output_dir / MANAGED_FILE,
        (json.dumps({"schema_version": MANAGED_SCHEMA_VERSION, "outputs": sorted(paths)}, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    return output_dir


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=branding.DEFAULT_MANIFEST)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUTPUT)
    arguments = parser.parse_args(argv)
    try:
        destination = generate(arguments.manifest, arguments.out_dir)
    except (BrandingAssetsError, branding.BrandingError, subprocess.CalledProcessError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"Generated branding assets in {destination}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

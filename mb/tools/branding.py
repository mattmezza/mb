#!/usr/bin/env python3
"""Validate the product branding manifest and generate isolated build inputs."""

import argparse
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import tomllib
import unicodedata
from xml.sax.saxutils import escape as xml_escape

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = ROOT / "mb" / "branding.toml"
DEFAULT_OUTPUT = ROOT / ".build" / "generated" / "branding"
CHROMIUM_SOURCE = ROOT / ".build" / "chromium" / "src"
SCHEMA_VERSION = 1
PRODUCT_KEYS = frozenset({"short_name", "full_name", "executable_name", "profile_directory_name", "url_scheme", "description", "application_id", "desktop_file_name"})
STATIC_OUTPUT_NAMES = frozenset(("branding.h", "branding.gni", "branding_strings.grdp", "packaging.json", "icon.svg"))
MANAGED_FILE = ".branding-managed.json"
SAFE_IDENTIFIER = re.compile(r"[a-z][a-z0-9-]*\Z")
SAFE_APPLICATION_ID = re.compile(r"[a-z][a-z0-9]*(?:\.[a-z][a-z0-9]*)+\Z")


class BrandingError(ValueError):
    """A manifest or output-directory error that should be shown to the user."""


def fail(path, key, message):
    raise BrandingError(f"manifest {path}: {key}: {message}")


def require_text(path, product, key):
    value = product.get(key)
    if type(value) is not str:
        fail(path, f"product.{key}", "must be a string")
    if not value:
        fail(path, f"product.{key}", "must not be empty")
    if value != value.strip():
        fail(path, f"product.{key}", "must not begin or end with whitespace")
    if any(unicodedata.category(character) == "Cc" for character in value):
        fail(path, f"product.{key}", "must not contain control characters")
    return value


def load_manifest(path):
    """Return a validated manifest dictionary."""
    try:
        source = path.read_bytes()
    except OSError as error:
        raise BrandingError(f"manifest {path}: manifest: {error}") from error
    try:
        document = tomllib.loads(source.decode("utf-8"))
    except (UnicodeDecodeError, tomllib.TOMLDecodeError) as error:
        raise BrandingError(f"manifest {path}: manifest: invalid TOML: {error}") from error
    expected_top_level = {"schema_version", "product"}
    unknown, missing = set(document) - expected_top_level, expected_top_level - set(document)
    if unknown:
        fail(path, "manifest", "unknown key(s): " + ", ".join(sorted(unknown)))
    if missing:
        fail(path, "manifest", "missing key(s): " + ", ".join(sorted(missing)))
    if type(document["schema_version"]) is not int:
        fail(path, "schema_version", "must be an integer")
    if document["schema_version"] != SCHEMA_VERSION:
        fail(path, "schema_version", f"must be {SCHEMA_VERSION}")
    product = document["product"]
    if type(product) is not dict:
        fail(path, "product", "must be a table")
    unknown, missing = set(product) - PRODUCT_KEYS, PRODUCT_KEYS - set(product)
    if unknown:
        fail(path, "product", "unknown key(s): " + ", ".join(sorted(unknown)))
    if missing:
        fail(path, "product", "missing key(s): " + ", ".join(sorted(missing)))
    values = {key: require_text(path, product, key) for key in sorted(PRODUCT_KEYS)}
    for key in ("short_name", "executable_name", "profile_directory_name", "url_scheme"):
        if not SAFE_IDENTIFIER.fullmatch(values[key]):
            fail(path, f"product.{key}", "must be a lowercase path-safe identifier")
    if not SAFE_APPLICATION_ID.fullmatch(values["application_id"]):
        fail(path, "product.application_id", "must be a lowercase reverse-DNS identifier")
    desktop = values["desktop_file_name"]
    if not desktop.endswith(".desktop") or not SAFE_IDENTIFIER.fullmatch(desktop[:-8]):
        fail(path, "product.desktop_file_name", "must be a lowercase path-safe .desktop filename")
    return {"schema_version": SCHEMA_VERSION, "product": values}


def cxx_string(value):
    return value.replace("\\", "\\\\").replace('"', '\\"')


def gn_string(value):
    """Escape a GN double-quoted string without allowing $ expansion."""
    return cxx_string(value).replace("$", "\\$")


def desktop_string(value):
    return value.replace("\\", "\\\\").replace(";", "\\;")


def render(manifest):
    product = manifest["product"]
    escaped = {key: xml_escape(value, {'"': "&quot;"}) for key, value in product.items()}
    header = ["// Generated file; do not edit.", "#ifndef MB_GENERATED_BRANDING_H_", "#define MB_GENERATED_BRANDING_H_", "", "namespace mb::branding {"]
    for key in sorted(product):
        constant = "k" + "".join(part.capitalize() for part in key.split("_"))
        header.append(f'inline constexpr char {constant}[] = "{cxx_string(product[key])}";')
    header += ["}  // namespace mb::branding", "", "#endif  // MB_GENERATED_BRANDING_H_", ""]
    gni = ["# Generated file; do not edit."]
    gni += [f'branding_{key} = "{gn_string(product[key])}"' for key in sorted(product)]
    gni.append("")
    messages = ["<?xml version=\"1.0\" encoding=\"UTF-8\"?>", "<!-- Generated file; suitable for future GRIT inclusion. -->", "<grit-part>"]
    for key in sorted(product):
        messages.append(f'  <message name="IDS_PRODUCT_{key.upper()}" desc="Product branding value for {key}.">{escaped[key]}</message>')
    messages += ["</grit-part>", ""]
    desktop = "\n".join(["[Desktop Entry]", "Type=Application", f"Name={desktop_string(product['full_name'])}", f"Comment={desktop_string(product['description'])}", f"Exec=\"{product['executable_name']}\" %U", f"Icon={product['short_name']}", "Terminal=false", "Categories=Network;WebBrowser;", f"MimeType=text/html;application/xhtml+xml;x-scheme-handler/http;x-scheme-handler/https;x-scheme-handler/{product['url_scheme']};", f"StartupWMClass={product['application_id']}", ""])
    packaging = json.dumps({"schema_version": manifest["schema_version"], **product}, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    icon = "\n".join(["<?xml version=\"1.0\" encoding=\"UTF-8\"?>", "<!-- Original temporary abstract icon; generated from the branding manifest. -->", "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 128 128\" role=\"img\">", f"  <title>{escaped['full_name']}</title>", f"  <desc>{escaped['description']}</desc>", "  <rect x=\"10\" y=\"10\" width=\"108\" height=\"108\" rx=\"28\" fill=\"#1d2942\"/>", "  <path d=\"M31 38h66v52H31z\" fill=\"#d9e5ff\"/>", "  <path d=\"M31 38h66v13H31z\" fill=\"#5f7db5\"/>", "  <circle cx=\"42\" cy=\"44.5\" r=\"3\" fill=\"#f1c76a\"/>", "  <path d=\"M45 99 83 61l12 12-38 38z\" fill=\"#f1c76a\"/>", "</svg>", ""])
    return {"branding.h": "\n".join(header), "branding.gni": "\n".join(gni), "branding_strings.grdp": "\n".join(messages), product["desktop_file_name"]: desktop, "packaging.json": packaging, "icon.svg": icon}


def assert_safe_output_directory(output_dir):
    resolved, chromium = output_dir.resolve(), CHROMIUM_SOURCE.resolve()
    if resolved == chromium or chromium in resolved.parents:
        raise BrandingError("out-dir: refusing to generate inside .build/chromium/src")
    return resolved


def check_managed_directory(output_dir, output_names):
    if not output_dir.exists():
        return set()
    if not output_dir.is_dir():
        raise BrandingError(f"out-dir: not a directory: {output_dir}")
    marker, contents = output_dir / MANAGED_FILE, list(output_dir.iterdir())
    if not contents:
        return set()
    if marker.is_symlink() or not marker.is_file():
        raise BrandingError(f"out-dir: refusing nonempty unmanaged directory: {output_dir}")
    try:
        managed = json.loads(marker.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BrandingError(f"out-dir: invalid managed-output marker: {error}") from error
    if set(managed) != {"schema_version", "outputs"} or managed["schema_version"] != 1:
        raise BrandingError("out-dir: managed-output marker does not match this generator")
    old_names = managed["outputs"]
    if type(old_names) is not list or any(type(name) is not str for name in old_names):
        raise BrandingError("out-dir: managed-output marker does not match this generator")
    old_set = set(old_names)
    desktop_names = old_set - STATIC_OUTPUT_NAMES
    if (
        len(old_names) != len(old_set)
        or not STATIC_OUTPUT_NAMES <= old_set
        or len(desktop_names) != 1
        or any(Path(name).name != name for name in old_set)
        or not next(iter(desktop_names)).endswith(".desktop")
    ):
        raise BrandingError("out-dir: managed-output marker does not match this generator")
    planned_set = set(output_names)
    for name in planned_set:
        destination = output_dir / name
        if destination.is_symlink() or (destination.exists() and not destination.is_file()):
            raise BrandingError(f"out-dir: refusing unsafe output path: {destination}")
        if name not in old_set and (destination.exists() or destination.is_symlink()):
            raise BrandingError(f"out-dir: refusing existing non-owned output: {destination}")
    for name in old_set - planned_set:
        stale = output_dir / name
        if stale.is_symlink() or (stale.exists() and not stale.is_file()):
            raise BrandingError(f"out-dir: refusing to remove unsafe managed output: {stale}")
    return old_set


def atomic_write(path, content):
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(content)
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def generate(manifest_path, output_dir):
    manifest, outputs = load_manifest(manifest_path), None
    outputs = render(manifest)
    output_dir = assert_safe_output_directory(output_dir)
    previous_outputs = check_managed_directory(output_dir, outputs)
    output_dir.mkdir(parents=True, exist_ok=True)
    for name in sorted(outputs):
        atomic_write(output_dir / name, outputs[name])
    for name in previous_outputs - set(outputs):
        stale = output_dir / name
        if stale.exists():
            stale.unlink()
    atomic_write(output_dir / MANAGED_FILE, json.dumps({"schema_version": 1, "outputs": sorted(outputs)}, indent=2, sort_keys=True) + "\n")
    return output_dir


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUTPUT)
    arguments = parser.parse_args(argv)
    try:
        destination = generate(arguments.manifest, arguments.out_dir)
    except BrandingError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"Generated branding inputs in {destination}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

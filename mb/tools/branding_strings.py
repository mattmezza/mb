#!/usr/bin/env python3
"""Derive product strings from pinned Chromium inputs, without editing Chromium."""

import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tomllib
import xml.etree.ElementTree as ET

import branding

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = ROOT / ".build/chromium/src"
DEFAULT_OUTPUT = ROOT / ".build/generated/branding-strings"
PIN = ROOT / "mb/upstream-version.toml"
RECEIPT = "receipt.json"
SOURCES = (
    "chrome/app/chromium_strings.grd",
    "chrome/app/settings_chromium_strings.grdp",
    "components/components_chromium_strings.grd",
)
OUTPUT_NAMES = frozenset(Path(p).name for p in SOURCES) | {"first_ids.py"}
PROTECTED_IDS = frozenset({
    "IDS_ABOUT_VERSION_COMPANY_NAME", "IDS_ABOUT_VERSION_COPYRIGHT",
    "IDS_ABOUT_CROS_VERSION_LICENSE", "IDS_ABOUT_CROS_WITH_LINUX_VERSION_LICENSE",
})
LICENSE_IDS = frozenset({
    "IDS_VERSION_UI_LICENSE", "IDS_VERSION_UI_LICENSE_CHROMIUM",
    "IDS_VERSION_UI_LICENSE_OTHER",
})
TOKEN = re.compile(r"\bChromium\b")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def blob_digest(data):
    """Git SHA-1 blob identity for the exact input bytes (not a filtered file)."""
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()


def verify_pinned_inputs(source, commit, blobs):
    # One path-limited tree lookup; no checkout writes or whole-tree status scan.
    result = subprocess.run(
        ["git", "--no-replace-objects", "-C", str(source), "ls-tree", "-z",
         "--full-tree", commit, "--", *sorted(blobs)],
        check=True, capture_output=True)
    expected = {}
    for record in result.stdout.split(b"\0"):
        if record:
            metadata, path = record.split(b"\t", 1)
            mode, kind, object_id = metadata.split()
            name = path.decode("utf-8")
            if kind != b"blob" or mode not in {b"100644", b"100755"}:
                raise branding.BrandingError(f"source: pinned input is not a regular file: {name}")
            expected[name] = object_id.decode("ascii")
    for path, actual in sorted(blobs.items()):
        if expected.get(path) != actual:
            raise branding.BrandingError(
                f"source: input differs from pinned Git blob at {commit}: {path}")
    if set(expected) != set(blobs):
        raise branding.BrandingError("source: unexpected files in pinned input lookup")


def parse_xml(data):
    return ET.fromstring(data, parser=ET.XMLParser(
        target=ET.TreeBuilder(insert_comments=True)))


def serialize(root):
    return ET.tostring(root, encoding="utf-8", xml_declaration=True).decode() + "\n"


def transform(root, product):
    """Change ordinary message text and tails; never enter a ph/ex subtree."""
    changed = []
    for message in root.iter("message"):
        name = message.get("name", "")
        if name in PROTECTED_IDS or "COPYRIGHT" in name:
            continue
        before = ET.tostring(message)
        if name in {"IDS_PRODUCT_NAME", "IDS_SHORT_PRODUCT_NAME"}:
            if len(message):
                raise branding.BrandingError(f"source: unexpected children in {name}")
            value = product["full_name" if name == "IDS_PRODUCT_NAME" else "short_name"]
            text = message.text or ""
            message.text = text[:len(text) - len(text.lstrip())] + value + text[len(text.rstrip()):]
        elif name in LICENSE_IDS:
            # Only the leading distribution name changes. The project link
            # text is a later tail and must remain Chromium.
            text = message.text or ""
            if not re.match(r"\s*Chromium\b", text):
                raise branding.BrandingError(f"source: expected leading Chromium in {name}")
            message.text = TOKEN.sub(lambda _: product["full_name"], text, count=1)
        else:
            def rewrite(node):
                if node.tag in {"ph", "ex"} or not isinstance(node.tag, str):
                    return
                if node.text:
                    node.text = TOKEN.sub(lambda _: product["full_name"], node.text)
                for child in node:
                    rewrite(child)
                    if child.tail:
                        child.tail = TOKEN.sub(lambda _: product["full_name"], child.tail)
            rewrite(message)
        if ET.tostring(message) != before:
            changed.append(name)
    return changed


def prepare(manifest_path, source):
    manifest = branding.load_manifest(manifest_path)
    for key in ("full_name", "short_name"):
        # Names occur inside ICU patterns, HTML and keyboard mnemonics. Plain
        # text has no universal escape that works in every one of those contexts.
        if any(c in manifest["product"][key] for c in "%{}#'&<>[]"):
            branding.fail(manifest_path, f"product.{key}",
                          "unsupported formatting characters in branding strings: % { } # ' & < > [ ]")
    source = source.resolve(strict=True)
    pin = tomllib.loads(PIN.read_text())["chromium"]
    head = subprocess.run(["git", "-C", str(source), "rev-parse", "HEAD"],
                          check=True, text=True, capture_output=True).stdout.strip()
    if head != pin["commit"]:
        raise branding.BrandingError(f"source: expected pinned commit {pin['commit']}, found {head}")
    inputs, blobs, outputs, changes = {}, {}, {}, {}

    def read(path):
        data = path.read_bytes()
        name = path.relative_to(source).as_posix()
        current = blob_digest(data)
        if name in blobs and blobs[name] != current:
            raise branding.BrandingError(f"source: input changed while reading: {name}")
        inputs[name] = digest(data)
        blobs[name] = current
        return data

    for relative in SOURCES:
        path = source / relative
        root = parse_xml(read(path))
        changes[relative] = transform(root, manifest["product"])
        for node in root.findall("./translations/file"):
            original = node.get("path")
            resolved = (path.parent / original).resolve(strict=True)
            read(resolved)
            node.set("path", str(resolved))
        for node in root.iter("part"):
            target = (path.parent / node.get("file")).relative_to(source).as_posix()
            if target not in SOURCES:
                raise branding.BrandingError(f"source: unexpected GRIT part: {target}")
            node.set("file", Path(target).name)
        outputs[path.name] = serialize(root)

    spec_path = source / "tools/gritsettings/resource_ids.spec"
    spec = ast.literal_eval(read(spec_path).decode())
    verify_pinned_inputs(source, pin["commit"], blobs)
    mapping = {"SRCDIR": "."}
    for relative in SOURCES:
        if relative.endswith(".grd"):
            # Preserve the pinned allocation and any future size constraints.
            mapping[Path(relative).name] = spec[relative]
    outputs["first_ids.py"] = repr(mapping) + "\n"
    receipt = {
        "schema_version": 1, "generator": "mb-branding-strings",
        "chromium_commit": head, "chromium_version": pin["version"],
        "source_root": str(source), "source_sha256": dict(sorted(inputs.items())),
        "source_git_blobs": dict(sorted(blobs.items())),
        "manifest_sha256": digest(manifest_path.read_bytes()),
        "product": manifest["product"], "changed_messages": changes,
        "protected_message_ids": sorted(PROTECTED_IDS),
        "leading_product_only_ids": sorted(LICENSE_IDS),
        "translations": "Unmodified upstream XTB; changed fingerprints fall back to English.",
        "outputs": {name: digest(text.encode()) for name, text in sorted(outputs.items())},
    }
    outputs[RECEIPT] = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    return source, outputs


def check_output(output, source):
    # Reject aliases before resolve so a symlink cannot disguise a managed dir.
    for path in (output, *output.parents):
        if path.is_symlink():
            raise branding.BrandingError(f"out-dir: refusing symlink: {path}")
    resolved = output.resolve()
    for forbidden in (source, DEFAULT_SOURCE.resolve()):
        if resolved == forbidden or forbidden in resolved.parents:
            raise branding.BrandingError("out-dir: refusing to generate inside Chromium source")
    if not output.exists():
        return
    if not output.is_dir():
        raise branding.BrandingError(f"out-dir: not a directory: {output}")
    entries = {path.name: path for path in output.iterdir()}
    if not entries:
        return
    if set(entries) != OUTPUT_NAMES | {RECEIPT}:
        raise branding.BrandingError("out-dir: refusing nonempty unmanaged or unexpected directory contents")
    if any(p.is_symlink() or not p.is_file() for p in entries.values()):
        raise branding.BrandingError("out-dir: refusing unsafe managed output")
    try:
        previous = json.loads(entries[RECEIPT].read_text())
        if (not isinstance(previous, dict) or previous.get("schema_version") != 1 or
                previous.get("generator") != "mb-branding-strings" or
                not isinstance(previous.get("outputs"), dict) or
                set(previous["outputs"]) != OUTPUT_NAMES):
            raise ValueError("invalid receipt")
        for name, expected in previous["outputs"].items():
            if digest(entries[name].read_bytes()) != expected:
                raise ValueError(f"managed output modified: {name}")
    except (ValueError, KeyError, TypeError) as error:
        raise branding.BrandingError(f"out-dir: refusing invalid managed output: {error}") from error


def generate(manifest_path=branding.DEFAULT_MANIFEST, output=DEFAULT_OUTPUT,
             source=DEFAULT_SOURCE):
    source, outputs = prepare(Path(manifest_path), Path(source))
    output = Path(output).absolute()
    check_output(output, source)
    output.mkdir(parents=True, exist_ok=True)
    for name in sorted(OUTPUT_NAMES):
        branding.atomic_write(output / name, outputs[name])
    branding.atomic_write(output / RECEIPT, outputs[RECEIPT])
    return output


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=branding.DEFAULT_MANIFEST)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args(argv)
    try:
        destination = generate(args.manifest, args.out_dir, args.source)
    except (branding.BrandingError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"Generated standalone branding strings in {destination}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

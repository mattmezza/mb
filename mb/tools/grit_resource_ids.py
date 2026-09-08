#!/usr/bin/env python3
"""Alias Chromium's resolved GRIT allocations for the product string inputs.

Production consumes the output of //tools/gritsettings:default_resource_ids.
The static first_ids.py from branding_strings.py is only a standalone test
fixture; resource_ids.spec contains allocation seeds, not final build IDs.
"""

import argparse
import ast
import copy
import hashlib
import json
import os
from pathlib import Path
import pprint
import sys
import tempfile


ALIASES = {
    "mb/generated/branding-strings/chromium_strings.grd":
        "chrome/app/chromium_strings.grd",
    "mb/generated/branding-strings/components_chromium_strings.grd":
        "components/components_chromium_strings.grd",
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def derive(input_path, source_root):
    """Copy complete resolved entries, preserving grouping arrays and metadata."""
    source_root = source_root.resolve(strict=True)
    data = input_path.read_bytes()
    try:
        resolved = ast.literal_eval(data.decode("utf-8"))
    except (SyntaxError, ValueError, UnicodeError) as error:
        raise ValueError(f"input: invalid GRIT allocation dictionary: {error}") from error
    if not isinstance(resolved, dict) or not isinstance(resolved.get("SRCDIR"), str):
        raise ValueError("input: expected an allocation dictionary with SRCDIR")
    mapped_source = (input_path.resolve().parent / resolved["SRCDIR"]).resolve()
    if mapped_source != source_root:
        raise ValueError("input: SRCDIR does not match --source-root")
    aliases = {"SRCDIR": str(source_root)}
    originals = {}
    for alias, original in ALIASES.items():
        entry = resolved.get(original)
        if (not isinstance(entry, dict) or
                not isinstance(entry.get("messages"), list) or
                not entry["messages"] or
                any(type(value) is not int or value <= 0 for value in entry["messages"])):
            raise ValueError(f"input: missing or invalid resolved messages allocation: {original}")
        aliases[alias] = copy.deepcopy(entry)
        originals[original] = copy.deepcopy(entry)
    output = ("# Generated from Chromium's resolved resource ID map; do not edit.\n" +
              pprint.pformat(aliases, sort_dicts=True, width=100) + "\n").encode("utf-8")
    receipt = {
        "schema_version": 1,
        "input": str(input_path.resolve()),
        "input_sha256": digest(data),
        "source_root": str(source_root),
        "aliases": ALIASES,
        "resolved_allocations": originals,
        "output_sha256": digest(output),
    }
    return output, (json.dumps(receipt, indent=2, sort_keys=True) + "\n").encode("utf-8")


def check_destination(path):
    for item in (path, *path.parents):
        if item.is_symlink():
            raise ValueError(f"output: refusing symlink destination: {item}")
    if path.exists() and not path.is_file():
        raise ValueError(f"output: destination is not a regular file: {path}")


def atomic_write(path, data):
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".grit-resource-ids-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def generate(input_path, source_root, output_path, receipt_path):
    paths = [path.resolve() for path in (input_path, output_path, receipt_path)]
    if len(set(paths)) != len(paths):
        raise ValueError("output: input, allocation output and receipt must be distinct files")
    output, receipt = derive(input_path, source_root)
    for path in (output_path, receipt_path):
        check_destination(path)
    atomic_write(output_path, output)
    atomic_write(receipt_path, receipt)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--receipt", required=True, type=Path)
    args = parser.parse_args()
    try:
        generate(args.input, args.source_root, args.output, args.receipt)
    except (ValueError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

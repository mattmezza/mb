#!/usr/bin/env python3
"""Stage a receipt-bound Chromium release as an Arch package payload."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tomllib


ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)


def fail(message: str) -> None:
    raise RuntimeError(message)


def file_sha(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def load_manifest(path: Path) -> dict[str, str]:
    product = tomllib.loads(path.read_text(encoding="utf-8")).get("product")
    required = {
        "short_name", "full_name", "description", "executable_name",
        "application_id", "profile_directory_name", "desktop_file_name",
        "url_scheme",
    }
    if (not isinstance(product, dict) or set(product) != required or
            any(not isinstance(value, str) or not value for value in product.values())):
        fail("branding manifest has an invalid product table")
    for key in ("short_name", "executable_name", "profile_directory_name",
                "desktop_file_name"):
        if "/" in product[key] or "\\" in product[key] or product[key] in (".", ".."):
            fail(f"branding key {key} is not path-safe")
    return product


def load_product_module(repo_root: Path):
    sys.path.insert(0, str(repo_root / "mb/tools"))
    import product  # pylint: disable=import-outside-toplevel
    return product


def validate_receipt(repo_root: Path, output: Path, receipt_path: Path,
                     manifest: dict[str, str]):
    product = load_product_module(repo_root)
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if receipt.get("stage") != "build" or receipt.get("exit_code") != 0:
        fail("a successful release build receipt is required")
    integration = product.verify()
    if receipt.get("integration") != integration:
        fail("release receipt differs from the current verified integration")
    if receipt.get("integration_sha256") != file_sha(product.RECEIPT):
        fail("release receipt integration hash differs from product receipt")
    profile, binary = product.expected_product_binary(
        receipt, manifest["executable_name"])
    if profile["name"] != "release" or profile["output"].resolve() != output:
        fail("release receipt does not identify this output directory")
    if not binary.is_file() or not os.access(binary, os.X_OK):
        fail("manifest-derived release executable is missing")
    if receipt.get("binary_sha256") != file_sha(binary):
        fail("release executable hash differs from receipt")
    args_path = output / "args.gn"
    if args_path.read_text(encoding="utf-8") != receipt.get("args_gn"):
        fail("release output arguments differ from receipt")
    return receipt, product


def resolved_args(product, output: Path) -> dict[str, bool]:
    relative = output.relative_to(product.SOURCE)
    run = subprocess.run(
        [str(product.DEPOT / "gn"), "args", str(relative), "--list", "--short"],
        cwd=product.SOURCE, check=False, capture_output=True, text=True, timeout=60)
    if run.returncode:
        fail("cannot resolve release GN arguments: " + run.stderr.strip())
    values = {}
    for line in run.stdout.splitlines():
        name, separator, value = line.partition("=")
        if separator and value.strip() in ("true", "false"):
            values[name.strip()] = value.strip() == "true"
    required = ("use_static_angle", "angle_shared_libvulkan", "enable_swiftshader")
    if any(name not in values for name in required):
        fail("release GN argument inventory is incomplete")
    return values


def add_file(files, source: Path, relative: str, mode: int, *, optional=False):
    if source.is_file():
        files.append((source, Path(relative), mode))
    elif not optional:
        fail(f"required release payload is missing: {source}")


def payload(output: Path, repo_root: Path, manifest: dict[str, str],
            args: dict[str, bool]):
    files = []
    add_file(files, output / manifest["executable_name"],
             manifest["executable_name"], 0o755)
    add_file(files, output / "chrome_sandbox", "chrome-sandbox", 0o4755)
    add_file(files, output / "chrome_crashpad_handler",
             "chrome_crashpad_handler", 0o755)
    add_file(files, output / "chrome_management_service",
             "chrome-management-service", 0o755)
    for name in ("resources.pak", "icudtl.dat"):
        add_file(files, output / name, name, 0o644)
    if (output / "chrome_100_percent.pak").is_file():
        add_file(files, output / "chrome_100_percent.pak", "chrome_100_percent.pak", 0o644)
        add_file(files, output / "chrome_200_percent.pak", "chrome_200_percent.pak", 0o644)
    else:
        add_file(files, output / "theme_resources_100_percent.pak",
                 "theme_resources_100_percent.pak", 0o644)
        add_file(files, output / "ui_resources_100_percent.pak",
                 "ui_resources_100_percent.pak", 0o644)
    snapshot = next((output / name for name in
                     ("v8_context_snapshot.bin", "snapshot_blob.bin")
                     if (output / name).is_file()), None)
    if snapshot is None:
        fail("release output lacks a V8 snapshot")
    add_file(files, snapshot, snapshot.name, 0o644)
    locales = sorted((output / "locales").glob("*.pak"))
    if not any(path.name == "en-US.pak" for path in locales):
        fail("release output lacks locales/en-US.pak")
    for locale in locales:
        add_file(files, locale, f"locales/{locale.name}", 0o644)
    if not args["use_static_angle"]:
        add_file(files, output / "libEGL.so", "libEGL.so", 0o755)
        add_file(files, output / "libGLESv2.so", "libGLESv2.so", 0o755)
    if args["angle_shared_libvulkan"]:
        add_file(files, output / "libvulkan.so.1", "libvulkan.so.1", 0o755)
    if args["enable_swiftshader"]:
        add_file(files, output / "libvk_swiftshader.so", "libvk_swiftshader.so", 0o755)
        add_file(files, output / "vk_swiftshader_icd.json", "vk_swiftshader_icd.json", 0o644)
    for name in ("lib/libc++.so", "libqt5_shim.so", "libqt6_shim.so",
                 "liboptimization_guide_internal.so", "libLiteRtWebGpuAccelerator.so"):
        add_file(files, output / name, name, 0o755, optional=True)
    for directory, names in {
        "MEIPreload": ("manifest.json", "preloaded_data.pb"),
        "PrivacySandboxAttestationsPreloaded":
            ("manifest.json", "privacy-sandbox-attestations.dat"),
    }.items():
        if (output / directory).is_dir():
            for name in names:
                add_file(files, output / directory / name, f"{directory}/{name}", 0o644)
    add_file(files, repo_root / "LICENSE", "LICENSE", 0o644)
    add_file(files, output / "credits.html", "credits.html", 0o644)
    return files


def stage(repo_root: Path, output: Path, receipt_path: Path, destination: Path):
    manifest = load_manifest(repo_root / "mb/branding.toml")
    receipt, product = validate_receipt(repo_root, output, receipt_path, manifest)
    args = resolved_args(product, output)
    install_root = destination / "opt" / manifest["profile_directory_name"]
    for source, relative, mode in payload(output, repo_root, manifest, args):
        if relative.is_absolute() or ".." in relative.parts:
            fail(f"unsafe staging destination: {relative}")
        target = install_root / relative
        target.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        target.chmod(mode)
    desktop = repo_root / ".build/generated/branding" / manifest["desktop_file_name"]
    add_file([], desktop, desktop.name, 0o644)
    applications = destination / "usr/share/applications"
    applications.mkdir(mode=0o755, parents=True, exist_ok=True)
    shutil.copyfile(desktop, applications / desktop.name)
    icons = repo_root / ".build/generated/branding-assets/unscaled"
    for size in ICON_SIZES:
        source = icons / f"product_logo_{size}.png"
        if not source.is_file():
            fail(f"generated icon is missing: {source}")
        target = destination / f"usr/share/icons/hicolor/{size}x{size}/apps/{manifest['short_name']}.png"
        target.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        target.chmod(0o644)
    bin_dir = destination / "usr/bin"
    bin_dir.mkdir(mode=0o755, parents=True, exist_ok=True)
    os.symlink(f"/opt/{manifest['profile_directory_name']}/{manifest['executable_name']}",
               bin_dir / manifest["executable_name"])
    license_dir = destination / "usr/share/licenses" / manifest["short_name"]
    license_dir.mkdir(mode=0o755, parents=True, exist_ok=True)
    shutil.copyfile(install_root / "LICENSE", license_dir / "LICENSE")
    return {
        "binary_sha256": receipt["binary_sha256"],
        "files": sum(1 for path in destination.rglob("*") if path.is_file()),
        "sandbox_mode": oct(stat.S_IMODE((install_root / "chrome-sandbox").stat().st_mode)),
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--build-receipt", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args(argv)
    result = stage(args.repo_root.resolve(), args.output_dir.resolve(),
                   args.build_receipt.resolve(), args.destination.resolve())
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, KeyError,
            json.JSONDecodeError, subprocess.TimeoutExpired) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)

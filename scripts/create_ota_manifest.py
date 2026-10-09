#!/usr/bin/env python3
"""Publish a platform-safe OTA manifest for one board build."""

import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

DEVICE_INFO_PATTERN = re.compile(
    r'constexpr\s+(?:[A-Za-z_]\w*::)*DeviceInfo\s+kDeviceInfo\s*\{\s*'
    r'"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,', re.DOTALL)
BOARD_PROVIDER_PATTERN = re.compile(r'Board&\s+boardImplementation\s*\(')
PLATFORMS = ("xiao_esp32c3", "xiao_esp32c6")


def selected_device_info(project_root: Path, platform: str) -> Path:
    bsp_root = project_root / "src/bsp"
    matches = [source for source in bsp_root.rglob("board_*.cpp")
               if platform in source.parts
               if BOARD_PROVIDER_PATTERN.search(source.read_text(encoding="utf-8"))]
    if len(matches) != 1:
        raise ValueError(f"expected one Board implementation for {platform}, found {matches!r}")
    source = matches[0].parent / "device_info.cpp"
    if not source.is_file():
        source = bsp_root / "ampere" / "common" / "terra" / "device_info.cpp"
    if not source.is_file():
        raise ValueError(f"selected BSP has no device identity source: {source}")
    return source


def read_device_info(source: Path, platform: str) -> dict[str, str]:
    match = DEVICE_INFO_PATTERN.search(source.read_text(encoding="utf-8"))
    if not match:
        raise ValueError(f"could not read Board::DeviceInfo initializer from {source}")
    name, codename, manufacturer = match.groups()
    if not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", codename):
        raise ValueError("codename must be a lowercase identifier")
    if platform not in PLATFORMS:
        raise ValueError(f"unsupported platform {platform!r}")
    return {"device_name": name, "codename": codename,
            "manufacturer": manufacturer, "platform": platform}


def create_manifest(image: Path, tag: str, identity: dict[str, str],
                    bootloader: Path | None = None, partitions: Path | None = None,
                    boot_app0: Path | None = None, site_root: Path | None = None) -> dict:
    if not re.fullmatch(r"ewp-[A-Za-z0-9._-]+", tag):
        raise ValueError("release tag must start with ewp-")
    payload = image.read_bytes()
    manifest = {
        "schema": 2 if all((bootloader, partitions, boot_app0, site_root)) else 1,
        "device_name": identity["device_name"],
        "codename": identity["codename"],
        "manufacturer": identity["manufacturer"],
        "platform": identity["platform"],
        "tag": tag,
        "version": tag,
        "firmware_url": f"https://pkgs-wearables.ersa.dev/firmware/{identity['codename']}/{identity['platform']}/{tag}.bin",
        "sha256": hashlib.sha256(payload).hexdigest(),
        "size": len(payload),
    }
    if all((bootloader, partitions, boot_app0, site_root)):
        migration_root = site_root / "migration" / identity["codename"] / identity["platform"] / tag
        migration_files = {
            "bootloader": (bootloader, "bootloader.bin", 0x0),
            "partitions": (partitions, "partitions.bin", 0x8000),
            "boot_app0": (boot_app0, "boot_app0.bin", 0xE000),
        }
        migration = {}
        for key, (source, filename, offset) in migration_files.items():
            destination = migration_root / filename
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            contents = source.read_bytes()
            migration[key] = {
                "url": ("https://pkgs-wearables.ersa.dev/migration/"
                        f"{identity['codename']}/{identity['platform']}/{tag}/{filename}"),
                "sha256": hashlib.sha256(contents).hexdigest(),
                "size": len(contents),
                "address": offset,
            }
        manifest["migration"] = migration
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware", type=Path)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--platform", choices=PLATFORMS, required=True)
    parser.add_argument("--project-root", type=Path, default=Path("."))
    parser.add_argument("--site-root", required=True, type=Path)
    parser.add_argument("--bootloader", type=Path)
    parser.add_argument("--partitions", type=Path)
    parser.add_argument("--boot-app0", type=Path)
    args = parser.parse_args()
    identity = read_device_info(selected_device_info(args.project_root, args.platform), args.platform)
    migration_arguments = (args.bootloader, args.partitions, args.boot_app0)
    if any(migration_arguments) and not all(migration_arguments):
        parser.error("--bootloader, --partitions, and --boot-app0 must be provided together")
    manifest = create_manifest(args.firmware, args.tag, identity, *migration_arguments,
                               args.site_root if all(migration_arguments) else None)
    firmware_path = args.site_root / "firmware" / identity["codename"] / identity["platform"] / f"{args.tag}.bin"
    manifest_path = args.site_root / "ota" / identity["codename"] / identity["platform"] / "ota.json"
    firmware_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.firmware, firmware_path)
    manifest_path.write_text(json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

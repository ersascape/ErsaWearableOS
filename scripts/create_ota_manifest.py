#!/usr/bin/env python3
"""Create the small OTA manifest published alongside each firmware release."""

import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

DEVICE_INFO_PATTERN = re.compile(
    r'constexpr\s+(?:[A-Za-z_]\w*::)*DeviceInfo\s+kDeviceInfo\s*\{\s*'
    r'"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}',
    re.DOTALL,
)
BOARD_PROVIDER_PATTERN = re.compile(r'Board&\s+boardImplementation\s*\(')


def selected_device_info(project_root: Path) -> Path:
    bsp_root = project_root / "src/bsp"
    matches = [source for source in bsp_root.glob("**/board_*.cpp")
               if BOARD_PROVIDER_PATTERN.search(source.read_text(encoding="utf-8"))]
    if len(matches) != 1:
        raise ValueError(f"expected one selected Board implementation under {bsp_root}, found {matches!r}")
    source = matches[0].parent / "device_info.cpp"
    if not source.is_file():
        raise ValueError(f"selected BSP has no device identity source: {source}")
    return source


def read_device_info(source: Path) -> dict[str, str]:
    match = DEVICE_INFO_PATTERN.search(source.read_text(encoding="utf-8"))
    if not match:
        raise ValueError(f"could not read Board::DeviceInfo initializer from {source}")
    name, codename, manufacturer = match.groups()
    if not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", codename):
        raise ValueError("codename must be a lowercase identifier")
    return {"device_name": name, "codename": codename, "manufacturer": manufacturer}


def create_manifest(image: Path, tag: str, identity: dict[str, str]) -> dict:
    if not re.fullmatch(r"ewp-[A-Za-z0-9._-]+", tag):
        raise ValueError("release tag must start with ewp-")
    payload = image.read_bytes()
    return {
        "schema": 1,
        "device_name": identity["device_name"],
        "codename": identity["codename"],
        "manufacturer": identity["manufacturer"],
        "tag": tag,
        "version": tag,
        "firmware_url": f"https://pkgs-wearables.ersa.dev/firmware/{identity['codename']}/{tag}.bin",
        "sha256": hashlib.sha256(payload).hexdigest(),
        "size": len(payload),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware", type=Path)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--project-root", type=Path, default=Path("."),
                        help="project root; active BSP provides the Board contract under src/bsp")
    parser.add_argument("--site-root", required=True, type=Path,
                        help="write firmware/<codename>/<tag>.bin and ota/<codename>/ota.json here")
    args = parser.parse_args()
    identity = read_device_info(selected_device_info(args.project_root))
    manifest = create_manifest(args.firmware, args.tag, identity)
    firmware_path = args.site_root / "firmware" / identity["codename"] / f"{args.tag}.bin"
    manifest_path = args.site_root / "ota" / identity["codename"] / "ota.json"
    firmware_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.firmware, firmware_path)
    manifest_path.write_text(json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

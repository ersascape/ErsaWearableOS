#!/usr/bin/env python3
"""Package PlatformIO outputs as an app update and a full factory image."""

import argparse
from hashlib import sha256
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PARTITION_OFFSET = 0x8000
BOOT_APP0_OFFSET = 0xE000
DEFAULT_APPLICATION_OFFSET = 0x10000


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--environment", default="ErsaWearable")
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    platform = "xiao_esp32c6" if args.environment.endswith("C6") else "xiao_esp32c3"
    chip = "esp32c6" if platform.endswith("c6") else "esp32c3"
    image_prefix = f"terra-{platform}"
    build_dir = ROOT / ".pio" / "build" / args.environment
    output_dir = args.output_dir or ROOT / ".pio" / "release"
    files = {
        "bootloader.bin": build_dir / "bootloader.bin",
        "partitions.bin": build_dir / "partitions.bin",
        "boot_app0.bin": ROOT / ".pio-core" / "packages" / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin",
        "firmware.bin": build_dir / "firmware.bin",
    }
    missing = [str(path) for path in files.values() if not path.is_file()]
    if missing:
        raise SystemExit("Missing PlatformIO output(s): " + ", ".join(missing))

    output_dir.mkdir(parents=True, exist_ok=True)
    partition_data = files["partitions.bin"].read_bytes()
    partitions = [
        (
            partition_data[pos + 2],
            partition_data[pos + 3],
            int.from_bytes(partition_data[pos + 4 : pos + 8], "little"),
            int.from_bytes(partition_data[pos + 8 : pos + 12], "little"),
        )
        for pos in range(0, len(partition_data) - 31, 32)
        if partition_data[pos : pos + 2] == b"\xaa\x50" and partition_data[pos + 2] in (0, 1)
    ]
    ota_slots = {
        subtype: (offset, size) for kind, subtype, offset, size in partitions
        if kind == 0 and subtype in (0x10, 0x11)
    }
    if set(ota_slots) != {0x10, 0x11}:
        raise SystemExit(f"Expected exactly ota_0 and ota_1 app partitions; found {ota_slots!r}")
    app_partitions = sorted(ota_slots.values())
    if app_partitions[0][0] != DEFAULT_APPLICATION_OFFSET:
        raise SystemExit(f"Expected ota_0 at {DEFAULT_APPLICATION_OFFSET:#x}; found {app_partitions!r}")
    application_offset, app_size = app_partitions[0]
    if app_partitions[0][1] != app_partitions[1][1]:
        raise SystemExit("OTA app slots must have equal sizes")
    if files["firmware.bin"].stat().st_size > app_size:
        raise SystemExit(
            f"Firmware image ({files['firmware.bin'].stat().st_size} bytes) exceeds "
            f"an OTA slot ({app_size} bytes)"
        )
    coredump = next(
        ((offset, size) for kind, subtype, offset, size in partitions
         if kind == 1 and subtype == 3),
        None,
    )
    expected_coredump = (0x3F0000, 0x10000)
    if coredump != expected_coredump:
        raise SystemExit(f"Unexpected coredump partition for {platform}: {coredump!r}")

    firmware_name = f"firmware-{image_prefix}.bin"
    factory_name = f"ewp-factory-{image_prefix}.bin"
    (output_dir / firmware_name).write_bytes(files["firmware.bin"].read_bytes())
    for name in ("bootloader.bin", "partitions.bin", "boot_app0.bin"):
        stem, suffix = name.rsplit(".", 1)
        (output_dir / f"{stem}-{image_prefix}.{suffix}").write_bytes(files[name].read_bytes())

    end = application_offset + files["firmware.bin"].stat().st_size
    image = bytearray(b"\xff") * end
    image[: files["bootloader.bin"].stat().st_size] = files["bootloader.bin"].read_bytes()
    image[PARTITION_OFFSET : PARTITION_OFFSET + len(partition_data)] = partition_data
    boot_app0 = files["boot_app0.bin"].read_bytes()
    image[BOOT_APP0_OFFSET : BOOT_APP0_OFFSET + len(boot_app0)] = boot_app0
    app = files["firmware.bin"].read_bytes()
    image[application_offset : application_offset + len(app)] = app
    (output_dir / factory_name).write_bytes(image)

    names = (
        firmware_name,
        factory_name,
        f"bootloader-{image_prefix}.bin",
        f"partitions-{image_prefix}.bin",
        f"boot_app0-{image_prefix}.bin",
    )
    checksum_name = f"SHA256SUMS-{image_prefix}.txt"
    (output_dir / checksum_name).write_text(
        "".join(f"{sha256((output_dir / name).read_bytes()).hexdigest()}  {name}\n" for name in names),
        encoding="ascii",
    )
    (output_dir / f"FLASHING-{image_prefix}.md").write_text(
        f"""# Ersa Wearable firmware images

- `{firmware_name}` is the `{platform}` application image written to either OTA slot. Both slots
  are `{app_size:#x}` bytes; packaging fails if the firmware exceeds either slot.
- `{factory_name}` is a merged `{args.environment}` image for factory flashing at offset
  `0x0`. It includes the bootloader at `0x0`, the partition table at `0x8000`,
  the OTA boot data at `0xE000`, and the application at `0x10000`. Factory
  flashing erases existing settings.
- `{checksum_name}` contains SHA-256 checksums for all images.

Example factory flash with esptool:

```sh
esptool.py --chip {chip} --port PORT write_flash 0x0 {factory_name}
```

Replace `PORT` with the serial port for the watch. Check the image checksum
before flashing. Existing watches need a one-time partition migration before
OTA can work: place all release assets and `migrate_ota.sh` in one directory,
then run `migrate_ota.sh DIRECTORY PORT`. It writes the new
bootloader, partition table, OTA metadata, and app while preserving NVS settings.
This establishes the A/B slots and rollback support required by the on-device
updater. After migration, do not use the fixed-offset app flasher for routine
updates. The regular development upload command is `make firmware`.
""",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()

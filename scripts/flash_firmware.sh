#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
default_image="$repo_root/.pio/build/ErsaWearable/firmware.bin"

usage() {
  cat <<'EOF'
Usage: scripts/flash_firmware.sh [firmware.bin|ewp-factory.bin] [serial-port]

Pass either image downloaded from a GitHub Release. With no image argument,
the local app image is used when available, preserving saved watch settings.
A firmware.bin app update is written at 0x10000; an ewp-factory.bin image is
written at 0x0 and resets saved settings.
After migrating to the dual-slot layout, this app path always writes ota_0 and
cannot detect the active slot. Do not use it for routine updates; use the
on-device updater, which selects the inactive slot and verifies it before boot.

The serial port can also be set with PORT=/dev/ttyACM0.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

image="${1:-$default_image}"
if [[ ! -f "$image" && $# -eq 0 && -f "$repo_root/.pio/release/firmware.bin" ]]; then
  image="$repo_root/.pio/release/firmware.bin"
fi
if [[ ! -f "$image" ]]; then
  echo "Firmware image not found: $image" >&2
  usage >&2
  exit 2
fi

image="$(cd -- "$(dirname -- "$image")" && pwd)/$(basename -- "$image")"
filename="$(basename -- "$image")"
if [[ "$filename" == *xiao_esp32c6* || "$image" == *ErsaWearableC6* ]]; then
  chip=esp32c6
else
  chip=esp32c3
fi
if [[ "$filename" == *factory* ]]; then
  offset="0x0"
  image_kind="factory image (bootloader, partitions, and app)"
else
  offset="0x10000"
  image_kind="app image (uses the installed bootloader and partition table)"
fi

port="${2:-${PORT:-}}"
if [[ -z "$port" ]]; then
  shopt -s nullglob
  ports=(/dev/ttyACM* /dev/ttyUSB* /dev/cu.usbmodem* /dev/cu.usbserial*)
  if [[ ${#ports[@]} -eq 1 ]]; then
    port="${ports[0]}"
  else
    echo "Could not choose one serial port automatically." >&2
    printf 'Detected ports: %s\n' "${ports[*]:-(none)}" >&2
    echo "Pass the port as the second argument or set PORT." >&2
    exit 2
  fi
fi

esptool=()
if command -v esptool.py >/dev/null 2>&1; then
  esptool=(esptool.py)
elif command -v esptool >/dev/null 2>&1; then
  esptool=(esptool)
elif [[ -f "$repo_root/.pio-core/packages/tool-esptoolpy/esptool.py" ]]; then
  esptool=(python3 "$repo_root/.pio-core/packages/tool-esptoolpy/esptool.py")
else
  venv="$repo_root/.tools/esptool"
  if [[ ! -x "$venv/bin/esptool.py" ]]; then
    python3 -m venv "$venv"
    "$venv/bin/python" -m pip install 'esptool==4.9.0'
  fi
  esptool=("$venv/bin/esptool.py")
fi

echo "Flashing $filename as $image_kind to $port"
"${esptool[@]}" --chip "$chip" --port "$port" --baud 460800 \
  write_flash "$offset" "$image"

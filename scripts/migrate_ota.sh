#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
platform="${PLATFORM:-xiao_esp32c3}"
image_dir="$repo_root/.pio/release"
port="${PORT:-}"
positionals=()
while (($#)); do
  case "$1" in
    --platform)
      (($# >= 2)) || { echo "--platform requires xiao_esp32c3 or xiao_esp32c6" >&2; exit 2; }
      platform="$2"
      shift 2
      ;;
    -h|--help)
      break
      ;;
    *)
      positionals+=("$1")
      shift
      ;;
  esac
done
if ((${#positionals[@]} > 0)); then image_dir="${positionals[0]}"; fi
if ((${#positionals[@]} > 1)); then port="${positionals[1]}"; fi
if ((${#positionals[@]} > 2)); then echo "Too many positional arguments." >&2; exit 2; fi
case "$platform" in
  xiao_esp32c3) chip=esp32c3 ;;
  xiao_esp32c6) chip=esp32c6 ;;
  *) echo "Unsupported platform: $platform" >&2; exit 2 ;;
esac
suffix="terra-$platform"
checksum="SHA256SUMS-$suffix.txt"

usage() {
  cat <<'EOF'
Usage: scripts/migrate_ota.sh [--platform xiao_esp32c3|xiao_esp32c6] [release-directory] [serial-port]

Perform the one-time migration from the legacy single-slot partition table to
the dual-slot OTA layout. Requires platform-specific bootloader, partition,
boot_app0, firmware, and SHA256SUMS images from the same firmware build.
Checks all component hashes before flashing. NVS settings are preserved. This
overwrites the old SPIFFS region, which is unused by EWP.

The serial port may also be set with PORT=/dev/ttyACM0.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

for image in "bootloader-$suffix.bin" "partitions-$suffix.bin" "boot_app0-$suffix.bin" "firmware-$suffix.bin" "$checksum"; do
  if [[ ! -f "$image_dir/$image" ]]; then
    echo "Required migration image not found: $image_dir/$image" >&2
    exit 2
  fi
done

for image in "bootloader-$suffix.bin" "partitions-$suffix.bin" "boot_app0-$suffix.bin" "firmware-$suffix.bin"; do
  expected="$(awk -v name="$image" '$2 == name { print $1; exit }' "$image_dir/$checksum")"
  if [[ -z "$expected" ]]; then
    echo "No SHA-256 checksum recorded for $image" >&2
    exit 2
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    actual="$(sha256sum "$image_dir/$image" | awk '{ print $1 }')"
  else
    actual="$(shasum -a 256 "$image_dir/$image" | awk '{ print $1 }')"
  fi
  if [[ "$actual" != "$expected" ]]; then
    echo "SHA-256 verification failed for $image" >&2
    exit 2
  fi
done

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
  echo "esptool is required (install esptool or build with PlatformIO first)." >&2
  exit 2
fi

echo "Migrating $platform on $port to dual-slot OTA layout; keep USB connected until it completes."
echo "Saved Wi-Fi/settings are preserved. The unused old SPIFFS region becomes OTA slot 1."
"${esptool[@]}" --chip "$chip" --port "$port" --baud 460800 write_flash \
  0x10000 "$image_dir/firmware-$suffix.bin"
"${esptool[@]}" --chip "$chip" --port "$port" --baud 460800 write_flash \
  0x8000 "$image_dir/partitions-$suffix.bin"
"${esptool[@]}" --chip "$chip" --port "$port" --baud 460800 write_flash \
  0xE000 "$image_dir/boot_app0-$suffix.bin"
"${esptool[@]}" --chip "$chip" --port "$port" --baud 460800 write_flash \
  0x0 "$image_dir/bootloader-$suffix.bin"

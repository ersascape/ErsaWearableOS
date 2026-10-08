#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REF_NAME="${GITHUB_REF_NAME:-}"
BASE_VERSION=""
if [[ "$REF_NAME" =~ ^ewp-([0-9]+\.[0-9]+\.[0-9]+)$ ]]; then
  BASE_VERSION="${BASH_REMATCH[1]}"
else
  # Use the latest stable firmware tag as the package version baseline. The
  # package source itself tracks master, so this value only labels the build.
  LATEST_TAG="$(git ls-remote --tags --refs https://github.com/ersascape/ErsaWearableOS.git 'refs/tags/ewp-*' \
    | sed -n 's|.*refs/tags/||p' \
    | grep -E '^ewp-[0-9]+\.[0-9]+\.[0-9]+$' \
    | sort -V \
    | tail -n 1 || true)"
  if [[ "$LATEST_TAG" =~ ^ewp-([0-9]+\.[0-9]+\.[0-9]+)$ ]]; then
    BASE_VERSION="${BASH_REMATCH[1]}"
  else
    BASE_VERSION="$(sed -n 's/^pkgver=//p' "$ROOT/packaging/arch/ewctl/PKGBUILD" | head -n 1)"
  fi
fi
if [[ ! "$BASE_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Expected an ewp-X.Y.Z ref or valid pkgver in PKGBUILD; got '${REF_NAME:-unset}' / '$BASE_VERSION'" >&2
  exit 2
fi

BUILD_SHA="${GITHUB_SHA:-$(git -C "$ROOT" rev-parse HEAD)}"
BUILD_SHA="${BUILD_SHA:0:12}"
RUN_ID="${GITHUB_RUN_ID:-0}"
RUN_ATTEMPT="${GITHUB_RUN_ATTEMPT:-1}"
if [[ ! "$RUN_ID" =~ ^[0-9]+$ || ! "$RUN_ATTEMPT" =~ ^[0-9]+$ || ! "$BUILD_SHA" =~ ^[0-9a-fA-F]{7,12}$ ]]; then
  echo "Invalid build identity: run='$RUN_ID' attempt='$RUN_ATTEMPT' sha='$BUILD_SHA'" >&2
  exit 2
fi
PKGVER="${BASE_VERSION}.r${RUN_ID}.a${RUN_ATTEMPT}.g${BUILD_SHA,,}"

mkdir -p "$ROOT/.pio/release"
docker run --rm \
  -e PKGVER="$PKGVER" \
  -v "$ROOT:/workspace" \
  archlinux:base-devel \
  bash -euc '
    pacman -Syu --noconfirm --needed git pacman-contrib python python-pyserial python-rich esptool python-construct python-pygdbmi
    useradd --create-home builder
    mkdir -p /tmp/ewctl-pkgbuild /workspace/.pio/release
    cp /workspace/packaging/arch/ewctl/PKGBUILD /tmp/ewctl-pkgbuild/PKGBUILD
    sed -i "s/^pkgver=.*/pkgver=${PKGVER}/" /tmp/ewctl-pkgbuild/PKGBUILD
    chown -R builder:builder /tmp/ewctl-pkgbuild
    runuser -u builder -- bash -euc "cd /tmp/ewctl-pkgbuild && makepkg --cleanbuild --noconfirm"
    cp /tmp/ewctl-pkgbuild/*.pkg.tar.zst /workspace/.pio/release/
    repo-add /workspace/.pio/release/ersa-ewctl.db.tar.gz /workspace/.pio/release/ewctl-*.pkg.tar.zst
    # repo-add creates .db/.files aliases to the .tar.gz archives. Copy via
    # distinct temporary files before replacing those aliases with regular
    # files; direct cp follows the aliases and reports source == destination.
    cp -L /workspace/.pio/release/ersa-ewctl.db.tar.gz /tmp/ersa-ewctl.db
    mv -f /tmp/ersa-ewctl.db /workspace/.pio/release/ersa-ewctl.db
    cp -L /workspace/.pio/release/ersa-ewctl.files.tar.gz /tmp/ersa-ewctl.files
    mv -f /tmp/ersa-ewctl.files /workspace/.pio/release/ersa-ewctl.files
  '

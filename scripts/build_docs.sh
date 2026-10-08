#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

command -v mkdocs >/dev/null || { echo "mkdocs is required; install requirements-docs.txt" >&2; exit 1; }
command -v doxygen >/dev/null || { echo "doxygen is required" >&2; exit 1; }

mkdir -p .pio/doxygen
doxygen Doxyfile
mkdocs build --strict
mkdir -p .pio/wiki-site/api
cp -a .pio/doxygen/html/. .pio/wiki-site/api/

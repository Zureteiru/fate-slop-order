#!/usr/bin/env bash
# Builds build-switch/apps/switch/vnreader.nro inside the devkitpro/devkita64 Docker image.
#   bash tools/build_switch.sh            (Release)
#   bash tools/build_switch.sh Debug
set -euo pipefail
cd "$(dirname "$0")/.."
config="${1:-Release}"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W 2>/dev/null || pwd):/src" -w /src devkitpro/devkita64 sh -c "
  set -e
  /opt/devkitpro/portlibs/switch/bin/aarch64-none-elf-cmake -S . -B build-switch -DVNR_TARGET=switch -DCMAKE_BUILD_TYPE=$config
  cmake --build build-switch -j\$(nproc)
"
ls -l build-switch/apps/switch/vnreader.nro

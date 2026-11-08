#!/usr/bin/env bash
# Builds the PortMaster package for aarch64 handhelds (Anbernic RG28XX and other H700 / RK3566 devices):
#   bash tools/build_portmaster.sh   -> build-portmaster/vnreader.zip (ports/vnreader.sh + ports/vnreader/)
# Cross-compiles in Docker (tools/portmaster/Dockerfile). SDL2 is linked as libSDL2-2.0.so.0 and comes
# from the firmware; libstdc++ is linked statically; libcurl is loaded at run time if the device has it.
set -euo pipefail
cd "$(dirname "$0")/.."
src="$(pwd -W 2>/dev/null || pwd)"
docker build -q -t vnreader-portmaster tools/portmaster >/dev/null
MSYS_NO_PATHCONV=1 docker run --rm -v "$src:/src" -w /src vnreader-portmaster sh -c '
  set -e
  cmake -S . -B build-portmaster -DVNR_TARGET=desktop -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=tools/portmaster/aarch64-linux-gnu.cmake \
    -DVNR_SDL_SHARED=ON -DCMAKE_SKIP_RPATH=ON \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" >/dev/null
  cmake --build build-portmaster --target vnreader -j$(nproc)
  aarch64-linux-gnu-strip -o build-portmaster/vnreader.aarch64 build-portmaster/apps/desktop/vnreader
'
pkg=build-portmaster/package
rm -rf "$pkg" && mkdir -p "$pkg/vnreader/userdata"
cp tools/portmaster/vnreader.sh "$pkg/"
cp build-portmaster/vnreader.aarch64 tools/portmaster/README.md "$pkg/vnreader/"
touch "$pkg/vnreader/userdata/.keep"
(cd "$pkg" && rm -f ../vnreader.zip && python -c "
import os, zipfile
with zipfile.ZipFile('../vnreader.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for root, _, files in os.walk('.'):
        for f in files:
            p = os.path.join(root, f)
            info = zipfile.ZipInfo.from_file(p, os.path.relpath(p, '.'))
            info.external_attr = (0o755 if f.endswith(('.sh', '.aarch64')) else 0o644) << 16
            with open(p, 'rb') as fh: z.writestr(info, fh.read(), zipfile.ZIP_DEFLATED)
")
ls -l build-portmaster/vnreader.zip

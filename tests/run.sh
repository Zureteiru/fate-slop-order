#!/usr/bin/env bash
# vnreader test script: a task is done only when this passes.
# Builds the host tests with gcc + ASan/UBSan and runs them with ctest.
# On a machine without a Linux gcc it re-runs itself inside Docker.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${VNREADER_TEST_IMAGE:-devkitpro/devkita64:latest}"

if [ "${VNREADER_IN_CONTAINER:-}" != "1" ] && ! { [ "$(uname -s)" = "Linux" ] && command -v gcc >/dev/null; }; then
    echo "--- running in Docker ($IMAGE) ---"
    exec env MSYS_NO_PATHCONV=1 docker run --rm \
        -e VNREADER_IN_CONTAINER=1 -v "$ROOT:/src" -w /src "$IMAGE" bash tests/run.sh
fi

BUILD="${VNREADER_BUILD_DIR:-/tmp/vnreader-build}"
export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build "$BUILD" -j"$(nproc)"
ctest --test-dir "$BUILD" --output-on-failure

echo "TESTS: PASS"

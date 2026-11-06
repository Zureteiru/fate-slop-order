#!/usr/bin/env bash
# vnreader test script: a task is done only when this passes (all suites).
# Builds the host tests with gcc + ASan/UBSan and runs them with ctest.
# On a machine without a Linux gcc it re-runs itself inside Docker.
#
#   bash tests/run.sh                 all suites (dict_tests, core_tests, runtime_tests)
#   bash tests/run.sh runtime_tests   one suite while iterating (builds and runs only that)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${VNREADER_TEST_IMAGE:-devkitpro/devkita64:latest}"

if [ "${VNREADER_IN_CONTAINER:-}" != "1" ]; then
    bash "$ROOT/tests/fetch_fixtures.sh"
fi

if [ "${VNREADER_IN_CONTAINER:-}" != "1" ] && ! { [ "$(uname -s)" = "Linux" ] && command -v gcc >/dev/null; }; then
    echo "--- running in Docker ($IMAGE) ---"
    exec env MSYS_NO_PATHCONV=1 docker run --rm \
        -e VNREADER_IN_CONTAINER=1 -v "$ROOT:/src" -w /src "$IMAGE" bash tests/run.sh "$@"
fi

BUILD="${VNREADER_BUILD_DIR:-/tmp/vnreader-build}"
export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"

SUITE="${1:-}"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ >/dev/null
# Quiet build: the full log is printed only when the build fails.
if ! cmake --build "$BUILD" -j"$(nproc)" ${SUITE:+--target "$SUITE"} >"$BUILD/build.log" 2>&1; then
    cat "$BUILD/build.log"
    exit 1
fi
# The tests are built non-PIE to avoid gcc 12's ASan startup hang (see CMakeLists.txt). The
# timeout and retry stay as a safety net; real test failures are deterministic and fail every attempt.
ctest --test-dir "$BUILD" --output-on-failure --timeout 120 --repeat until-pass:3 ${SUITE:+-R "^$SUITE\$"}

echo "TESTS: PASS${SUITE:+ ($SUITE only; run all suites before calling a task done)}"

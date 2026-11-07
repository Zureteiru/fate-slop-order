#!/usr/bin/env bash
# Checks the vendored vnpop deinflector against hoshidicts (Hoshi Reader's Yomitan-style backend).
# Runs in the gcc:14 Docker image (hoshidicts needs C++23). hoshidicts is fetched once into
# scratch/hoshidicts (never committed) at the pinned commit below.
#
#   tools/dict_parity/run.sh transforms   # regenerate third_party/switch-vnpop/tools/yomitan_transforms.json
#   tools/dict_parity/run.sh deinflect    # deinflect the corpus with both and compare (exit 1 on a difference)
set -euo pipefail

HOSHI_COMMIT=e6af33e1ab3241e16b885ec81f048f05d81f766b
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MODE="${1:?usage: $0 transforms|deinflect}"

if [ "${PARITY_IN_CONTAINER:-}" != "1" ]; then
    exec env MSYS_NO_PATHCONV=1 docker run --rm -e PARITY_IN_CONTAINER=1 -v "$ROOT:/src" -w /src gcc:14 \
        bash tools/dict_parity/run.sh "$MODE"
fi

H=/src/scratch/hoshidicts
[ -d "$H/.git" ] || git clone -q https://github.com/Manhhao/hoshidicts "$H"
git -C "$H" -c advice.detachedHead=false checkout -q "$HOSHI_COMMIT"
git -C "$H" submodule update --init --depth 1 -q
# Only the deinflector is needed: it has no dependencies beyond utfcpp.
HFLAGS="-std=c++23 -O2 -I$H/include -I$H/external/utfcpp/source"
P=/src/tools/dict_parity
V=/src/third_party/switch-vnpop
W=/src/scratch/parity
mkdir -p "$W"

case "$MODE" in
transforms)
    g++ $HFLAGS "$P/dump_transforms.cpp" "$H/src/deinflector.cpp" -o /tmp/dump_transforms
    /tmp/dump_transforms > "$V/tools/yomitan_transforms.json"
    echo "wrote third_party/switch-vnpop/tools/yomitan_transforms.json"
    ;;
deinflect)
    g++ $HFLAGS "$P/deinflect_hoshi.cpp" "$H/src/deinflector.cpp" -o /tmp/deinflect_hoshi
    gcc -std=c11 -O2 -Wall -Wextra -I"$V/include" -I"$V/core/dict" "$P/deinflect_vnpop.c" "$V/core/dict/vnpop_deinflect.c" \
        -o /tmp/deinflect_vnpop
    python3 "$P/corpus.py" > "$W/corpus.txt"
    /tmp/deinflect_hoshi < "$W/corpus.txt" > "$W/hoshi.tsv"
    /tmp/deinflect_vnpop < "$W/corpus.txt" > "$W/vnpop.tsv"
    python3 "$P/compare.py" "$W/hoshi.tsv" "$W/vnpop.tsv" "$W/diff.txt"
    ;;
esac

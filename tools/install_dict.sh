#!/usr/bin/env bash
# Installs the Japanese dictionary vnreader looks words up in (Shift + mouse on PC, Y on Switch)
# into its user folder as dict.bin. Never committed. Without it, lookups show "No dictionary".
#
# Recommended: Yomitan .zip dictionaries, packed here with third_party/switch-vnpop/tools/dict_packer.py.
# Give a frequency list too (e.g. Jiten's jiten_freq_global.zip): it ranks the entries the way Hoshi
# Reader does (行った: 行く before 行う). Without one, JMdict's own scores decide.
#
#   tools/install_dict.sh "<path>/JMdict_english.zip" "<path>/jiten_freq_global.zip" [--dest <user dir>]
#   tools/install_dict.sh "<path>/safir/dist/dict.bin" [--dest <user dir>]   # a packed dict.bin as it is
set -euo pipefail

KNOWN=084438f24b646d181129e678b0f4649c5ef6f19c6be8a1bfefd66e5a1e623355  # Safir v0.1 dist/dict.bin (JMdict)

here="$(cd "$(dirname "$0")" && pwd)"
inputs=()
dest=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dest) dest="${2:?--dest needs a folder}"; shift 2 ;;
        *) inputs+=("$1"); shift ;;
    esac
done
[[ ${#inputs[@]} -gt 0 ]] || { echo "usage: $0 <dict.bin | yomitan .zip...> [--dest <user dir>]" >&2; exit 1; }
if [[ -z "$dest" ]]; then
    if [[ -d "$here/../userdata" ]]; then
        dest="$(cd "$here/../userdata" && pwd)"  # portable: the work folder's userdata
    elif [[ -n "${LOCALAPPDATA:-}" ]]; then
        dest="$LOCALAPPDATA/vnreader"
    else
        dest="$HOME/Library/Application Support/vnreader"
    fi
fi
mkdir -p "$dest"

if [[ "${inputs[0]}" == *.zip ]]; then
    py="$(command -v python3 || command -v python)"
    "$py" "$here/../third_party/switch-vnpop/tools/dict_packer.py" "${inputs[@]}" -o "$dest/dict.bin.tmp"
    mv "$dest/dict.bin.tmp" "$dest/dict.bin"
else
    src="${inputs[0]}"
    magic="$(head -c 4 "$src")"
    if [[ "$magic" != "VNPD" ]]; then  # VNPOP_DICT_MAGIC, little-endian
        echo "error: $src is not a vnpop dictionary (dict.bin) or a Yomitan .zip" >&2
        exit 1
    fi
    sum="$(sha256sum "$src" | cut -c1-64)"
    [[ "$sum" == "$KNOWN" ]] || echo "note: not the reference dict.bin ($sum); installing it anyway"
    cp "$src" "$dest/dict.bin"  # the name the app reads (kDictKey in runtime/include/vnr/rt/lookup.h)
fi
echo "installed $dest/dict.bin"

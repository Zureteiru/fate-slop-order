#!/usr/bin/env bash
# Installs the Japanese dictionary vnreader looks words up in (Shift + mouse on PC, Y on Switch)
# into its user folder as dict.bin. Never committed. Without it, lookups show "No dictionary".
#
# Either a packed vnpop dictionary (Safir's dist/dict.bin, sha256 below), or Yomitan .zip
# dictionaries (e.g. JMdict English), packed here with third_party/switch-vnpop/tools/dict_packer.py:
#
#   tools/install_dict.sh "<path>/safir/dist/dict.bin" [user dir]
#   tools/install_dict.sh "<path>/jmdict_english.zip" [user dir]
set -euo pipefail

KNOWN=084438f24b646d181129e678b0f4649c5ef6f19c6be8a1bfefd66e5a1e623355  # Safir v0.1 dist/dict.bin (JMdict)

src="${1:?usage: $0 <dict.bin | yomitan .zip> [user dir]}"
here="$(cd "$(dirname "$0")" && pwd)"
if [[ $# -ge 2 ]]; then
    dest="$2"
elif [[ -d "$here/../userdata" ]]; then
    dest="$(cd "$here/../userdata" && pwd)"  # portable: the work folder's userdata
elif [[ -n "${LOCALAPPDATA:-}" ]]; then
    dest="$LOCALAPPDATA/vnreader"
else
    dest="$HOME/Library/Application Support/vnreader"
fi
mkdir -p "$dest"

case "$src" in
    *.zip)
        py="$(command -v python3 || command -v python)"
        "$py" "$here/../third_party/switch-vnpop/tools/dict_packer.py" "$src" -o "$dest/dict.bin.tmp"
        mv "$dest/dict.bin.tmp" "$dest/dict.bin"
        ;;
    *)
        magic="$(head -c 4 "$src")"
        if [[ "$magic" != "VNPD" ]]; then  # VNPOP_DICT_MAGIC, little-endian
            echo "error: $src is not a vnpop dictionary (dict.bin) or a Yomitan .zip" >&2
            exit 1
        fi
        sum="$(sha256sum "$src" | cut -c1-64)"
        [[ "$sum" == "$KNOWN" ]] || echo "note: not the reference dict.bin ($sum); installing it anyway"
        cp "$src" "$dest/dict.bin"  # the name the app reads (kDictKey in runtime/include/vnr/rt/lookup.h)
        ;;
esac
echo "installed $dest/dict.bin"

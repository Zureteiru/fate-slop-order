#!/usr/bin/env bash
# Installs the game's dialogue font (FOT-Skip Std B, Fontworks) into vnreader's user folder.
# The font is commercial: it is never committed or downloaded by the app. Without it the app uses
# the OS's Japanese font.
#
#   tools/install_font.sh "<path>/FOT-Skip Std B.otf" [user dir]
set -euo pipefail

src="${1:?usage: $0 <FOT-Skip Std B.otf> [user dir]}"
if [[ $# -ge 2 ]]; then
    dest="$2"
elif [[ -d "$(dirname "$0")/../userdata" ]]; then
    dest="$(cd "$(dirname "$0")/../userdata" && pwd)"  # portable: the work folder's userdata
elif [[ -n "${LOCALAPPDATA:-}" ]]; then
    dest="$LOCALAPPDATA/vnreader"
else
    dest="$HOME/Library/Application Support/vnreader"
fi

if [[ "$(head -c 4 "$src")" != "OTTO" ]]; then
    echo "error: $src is not an OpenType (CFF) font" >&2
    exit 1
fi
mkdir -p "$dest/fonts"
cp "$src" "$dest/fonts/FOT-SkipStd-B.otf"   # the name the app reads (kGameFontKey in runtime/include/vnr/rt/app.h)
echo "installed $dest/fonts/FOT-SkipStd-B.otf"

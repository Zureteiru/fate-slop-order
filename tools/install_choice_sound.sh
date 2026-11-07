#!/usr/bin/env bash
# Installs the sound vnreader plays when a choice is picked into its user folder.
# It is game audio: never committed or downloaded by the app. Without it, choices are silent.
# The reference copy is Fate Café's fgochoice.mp3 (sha256 below); any MP3 works.
#
#   tools/install_choice_sound.sh "<path>/fgochoice.mp3" [user dir]
set -euo pipefail

KNOWN=01afaf6d6915bcb9bcb58898002cdf2e227ad296988264ca9615130add237673  # Fate Café fgochoice-CcVrEVPo.mp3

src="${1:?usage: $0 <choice sound .mp3> [user dir]}"
if [[ $# -ge 2 ]]; then
    dest="$2"
elif [[ -n "${LOCALAPPDATA:-}" ]]; then
    dest="$LOCALAPPDATA/vnreader"
else
    dest="$HOME/Library/Application Support/vnreader"
fi

magic="$(head -c 3 "$src" | od -An -tx1 | tr -d ' \n')"
if [[ "$magic" != "494433" && "${magic:0:3}" != "fff" ]]; then  # "ID3" tag or an MPEG frame sync
    echo "error: $src does not look like an MP3" >&2
    exit 1
fi
sum="$(sha256sum "$src" | cut -c1-64)"
[[ "$sum" == "$KNOWN" ]] || echo "note: not the reference file ($sum); installing it anyway"
mkdir -p "$dest/sfx"
cp "$src" "$dest/sfx/choice.mp3"   # the name the app reads (kChoiceSoundKey in runtime/include/vnr/rt/app.h)
echo "installed $dest/sfx/choice.mp3"

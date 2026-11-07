#!/usr/bin/env bash
# Copies the game's story-UI sheets out of the Spriters Resource "Fate/Grand Order - User Interface"
# zip into vnreader's user folder, so the app draws the real text box, name tab, choice frame and
# "next" marker. The sheets are game art: they are never committed or downloaded by the app.
#
#   tools/install_ui_sheets.sh <zip or unpacked folder> [user dir]
#
# Default user dir: %LOCALAPPDATA%\vnreader on Windows, ~/Library/Application Support/vnreader on
# macOS (the Switch and Android wrappers will document theirs).
set -euo pipefail

src="${1:?usage: $0 <User Interface zip or folder> [user dir]}"
if [[ $# -ge 2 ]]; then
    dest="$2"
elif [[ -d "$(dirname "$0")/../userdata" ]]; then
    dest="$(cd "$(dirname "$0")/../userdata" && pwd)"  # portable: the work folder's userdata
elif [[ -n "${LOCALAPPDATA:-}" ]]; then
    dest="$LOCALAPPDATA/vnreader"
else
    dest="$HOME/Library/Application Support/vnreader"
fi

# name in the rip | name vnreader reads | sha256 the piece rects in runtime/include/vnr/rt/ui_skin.h were measured on
sheets=(
    "003.png|003.png|c9bc1c59f0518868a398fe141dea15cd9296bc2584b5f8b553b8c14be85eb9d5"
    "030.png|030.png|f8c3fb4a0e15686a75eada0bab854ef5782540834c7b32728cb2f6ad64169e26"
)

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
if [[ -f "$src" ]]; then
    for entry in "${sheets[@]}"; do
        IFS='|' read -r name _ _ <<<"$entry"
        unzip -qjo "$src" "User Interface/$name" -d "$work"
    done
    from="$work"
else
    from="$src"
    [[ -d "$from/User Interface" ]] && from="$from/User Interface"
fi

mkdir -p "$dest/ui"
for entry in "${sheets[@]}"; do
    IFS='|' read -r name target sha <<<"$entry"
    got="$(sha256sum "$from/$name" | cut -d' ' -f1)"
    if [[ "$got" != "$sha" ]]; then
        echo "error: $name has sha256 $got, expected $sha (a different version of the sheet?)" >&2
        exit 1
    fi
    cp "$from/$name" "$dest/ui/$target"
    echo "installed $dest/ui/$target"
done

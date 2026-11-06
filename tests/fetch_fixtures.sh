#!/usr/bin/env bash
# Downloads the reference Atlas scripts used by the golden tests into .cache/scripts/.
# They are game text, so they are fetched, not committed. Each file is pinned by SHA-256:
# if Atlas updates a script the hash check fails loudly instead of tests passing on new text.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="$ROOT/.cache/scripts"
mkdir -p "$DIR"

# name | url | sha256 | optional byte range
A=https://static.atlasacademy.io/NA
FIXTURES=(
  "NA_0100000011.txt|$A/Script/01/0100000011.txt|225a4d5e39a5ddc7a6dcb86accc6e819b17b3d8bc0af321ac27dbc892119830e"
  "JP_0100000011.txt|https://static.atlasacademy.io/JP/Script/01/0100000011.txt|faa41b781c7697fb137111f895b37d7ed3b309b9759bbe63bbdf12adfc7feaf5"
  "NA_0100081810.txt|$A/Script/01/0100081810.txt|398b25f036d5d4fe39ff435a445a53393aaabae57bbae6368bef2689dc4f19c7"
  "NA_0100000010.txt|$A/Script/01/0100000010.txt|5d094092492f0e76f43e348e0df8728a1222112400a91d8ee4f9103d73a1a0f2"
  # runtime golden draw lists (Fuyuki part 1): images, svtScript and body sizes (first 32 bytes)
  "98001000_merged.png|$A/CharaFigure/98001000/98001000_merged.png|23320a01732bb2e3a71f1f65ca2fbd3a9a3e7f87ab53dbe3ac804c4414d51acb"
  "98004000_merged.png|$A/CharaFigure/98004000/98004000_merged.png|5294da4ae6a598455d6f5a8908ae7961fa7e16dfb5127d59bd9510b4f4480079"
  "body_98001000.bin|$A/CharaFigure/98001000/98001000.png|f9ba752283dd86d01c0a7818705f00d2956216597412f77d956f50619a587868|0-31"
  "body_98004000.bin|$A/CharaFigure/98004000/98004000.png|f9ba752283dd86d01c0a7818705f00d2956216597412f77d956f50619a587868|0-31"
  "back10310.png|$A/Back/back10310.png|2155333926e66dc2bb1ed7c59a9bf5a9caa4ed8f832e950efe180ba73e19faeb"
  "back10200.png|$A/Back/back10200.png|32e68ec9e493f8a154fab77df0dac388711d4fd5b68603a1a5fe7e1acf51ea0a"
  "svt_98001000_98004000.json|https://api.atlasacademy.io/raw/NA/svtScript?charaId=98001000&charaId=98004000|8a534012c19ebffd4f3c0ebf9fce96f53f7a637dc79a7523bd602b4c8f491069"
  # OFL font for headless text layout tests (the apps use the OS font instead)
  "MPLUS1p-Regular.ttf|https://github.com/google/fonts/raw/main/ofl/mplus1p/MPLUS1p-Regular.ttf|2f294ad496432b1608f070d310e3aa2adcf1de4af429f4901df97ec4bd361ed1"
)

sha() { sha256sum "$1" | cut -d' ' -f1 | tr -d '*'; }

for entry in "${FIXTURES[@]}"; do
  case "$entry" in \#*) continue ;; esac
  IFS='|' read -r name url want range <<< "$entry"
  file="$DIR/$name"
  if [ -f "$file" ] && [ "$(sha "$file")" = "$want" ]; then
    continue
  fi
  echo "fetching $name"
  if [ -n "${range:-}" ]; then
    curl -fsS -A "vnreader-tests" -r "$range" -o "$file.tmp" "$url"
  else
    curl -fsSL -A "vnreader-tests" -o "$file.tmp" "$url"
  fi
  got="$(sha "$file.tmp")"
  if [ "$got" != "$want" ]; then
    echo "ERROR: $name hash changed (Atlas updated the script?)" >&2
    echo "  want $want" >&2
    echo "  got  $got" >&2
    echo "  Re-check the golden tests against the new text, then update the hash in tests/fetch_fixtures.sh." >&2
    rm -f "$file.tmp"
    exit 1
  fi
  mv "$file.tmp" "$file"
  sleep 1
done
echo "fixtures ok"

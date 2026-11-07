#!/usr/bin/env bash
# Installs the 33 wipe textures (wipein / wipeout / wipeFilter masks) into vnreader's user folder.
# They are the greyscale masks Fate Café's reader ships (taken from the game), so they are never
# committed: each user installs them once. Without them the app draws procedural wipes.
#
#   tools/install_wipes.sh                 # fetch them from fatecafe.com (about 1.6 MB, 35 requests)
#   tools/install_wipes.sh <folder>        # or copy <name>.png files you already have
#   tools/install_wipes.sh <folder|-> <user dir>
#
# Every file must match the SHA-256 below (the app was tuned on these exact masks).
set -euo pipefail

PINS=(
  "cinema 4b630fabd098d44b5ce57cbbc1878cfbdab695a05c91da9e75b114c946260293"
  "circleIn 4a3e42e8a27c086a59c49c9cfe02c1e20beba68ca133f27da3809008b6a7ed07"
  "circleOut 5b18c44330473bf6234d6ff12ff23b7bc3fadcab12969acea146fcbc44fefced"
  "clash 328181e64a30963a08307cbddd6b028759278242fe56ffdee6103172beaa67c8"
  "cutAcross e7c78214b60edf1b7279c74e3fb9446dbb7217c41b60cab7fb3192096761b819"
  "damage e271024083a5fb100d49cfcea2468bc96e79f7f548c180b15597f472557f7e0d"
  "downToUp e738731a5266b24defd8555017363892f68d0bf725407d3c07b29d3b3f48efc0"
  "fire 1a75964ca83ad9dcde882ae89c409ae1cf1789c29b3a9cdfb253fe0deb85e3b6"
  "heartOutBig 26b0b739cb92e32d6c0615ada6d3eca8f4c4703fed4e1b1fc7c1be63ccd0f26e"
  "leftDownToRightUp 165a35d46f1dae0d938d82fbfe35b4ba81299983bca16bab0958422f67863602"
  "leftToRight 2af240aa018fb3fc6fc59f6298313d3063c02029ff5250352305c68d66d21019"
  "leftUpToRightDown a5b15bba31a744373a6844bcb75f34dbaffdc3e2cf79980017981538797955bd"
  "magic c6d0c66a890b32923a5ab6d9e8decf541802229103af76e3e3030b49e8a85c7b"
  "moya d97bcecc0a31f7071def427a161dfa1c5dbc86ddf52085d77880b2915f4d0f35"
  "openEye 86b9a474824a23ea1a6909460825c53f7fd36e0e381586bac7101429d053c4ba"
  "polka02 9ccfa2ab5d5d8e6ccc71d5bfa7df6ab3afaa93d704d3102f70d7f089bce6e76c"
  "polka04 d6b1f300ada6ac7cdacf5b033dca61f9285410c55aafcb055d78562610faf5bd"
  "rectangleStripDownToUp 8550fd538506dc69358c20742366be5261c166764e0a067f44fcf2a7b0e35b7b"
  "rectangleStripLeftToRight 159e182e281c512d1298a9259c653978d32312ec2f4fcfffc9d0f7a44a390ebd"
  "rectangleStripRightToLeft 77a704c66e1d7f1c06b670e56610cce7c050e643b3a176edb1eef5e4e4cd3dbb"
  "rectangleStripUpToDown 9f99bc5405ffef3bf5867ba856581013de239328b400dd3aa567360c4eb8b698"
  "rightDownToLeftUp 100b0d58cc2e43ac53ec338e93b6182c1eb8966822cd6f9f8061d482edf2f371"
  "rightToLeft 8b82292d52c693c1ae2b45bccc07a3c284795832e190de8ad200c5f325263780"
  "rightUpToLeftDown 52edec69ddbf92923fd79dd6e7768403e3ea23f8757d83b05166516dd72123ab"
  "rollFlashRight 04e0280b02204dab69b3627b69196a150a242ee56b280cdff81332a078eac8c5"
  "rollLeft a95ee39044d50a2e26432647befe8398f07a5c07f3958045e4fcc987cccfe481"
  "rollRight aad97ec91177546b5c5fe3948a3809ade9f04019f46583ea8883aec194363d13"
  "sazanami 1da90d9a3fc81aa647b665886f263754f81ad1b5cee34ed729f74da39ad1862d"
  "sideBlind 01e1f668bf6dc4c6139ba6f73b44d346e4fdd99af9f62a133d8213d0c11bd681"
  "upToDown 0d5a6ac03eec841bcd012e2bcc0e910a421d117c3f9a3b7498385122d89ca6be"
  "uzumaki 1ea1cbcdb352c389b6d69134099deaaca36f47c72c4bd1dcdbb49b68df9019d6"
  "uzumakiBig 97b18f6c9e7d49ed798df85a701dc3a204a8b24c4d861d00c21c7174a1095532"
  "windmill a9947922be92e522d69f1de50aa921f015eaf29599dcf233d8645ce2aa643438"
)

src="${1:--}"
if [[ $# -ge 2 ]]; then
    dest="$2"
elif [[ -d "$(dirname "$0")/../userdata" ]]; then
    dest="$(cd "$(dirname "$0")/../userdata" && pwd)"  # portable: the work folder's userdata
elif [[ -n "${LOCALAPPDATA:-}" ]]; then
    dest="$LOCALAPPDATA/vnreader"
else
    dest="$HOME/Library/Application Support/vnreader"
fi

sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -c1-64; else shasum -a 256 "$1" | cut -c1-64; fi; }
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

if [[ "$src" == "-" ]]; then
    site=https://fatecafe.com
    get() { sleep 0.3; curl -fsSL -A "Mozilla/5.0" "$1"; }
    index="$(get "$site/leitor" | grep -o '/assets/index\.[A-Za-z0-9_-]*\.js' | head -1)"
    view="$(get "$site$index" | grep -o 'ReaderChapterView\.[A-Za-z0-9_-]*\.js' | head -1)"
    [[ -n "$view" ]] || { echo "error: could not find Fate Café's reader chunk" >&2; exit 1; }
    get "$site/assets/$view" > "$tmp/view.js"
    for pin in "${PINS[@]}"; do
        name="${pin%% *}"
        var="$(grep -o "\"\.\./\.\./assets/fgo/wipes/$name\.png\":[A-Za-z0-9_$]*" "$tmp/view.js" | sed 's/.*://')"
        ref="$(grep -o "[,; ]$var=\"[^\"]*\"" "$tmp/view.js" | head -1 | sed "s/^.$var=\"//; s/\"$//")"
        case "$ref" in
            data:image/png\;base64,*) printf '%s' "${ref#*,}" | base64 --decode > "$tmp/$name.png" ;;
            /assets/*) get "$site$ref" > "$tmp/$name.png" ;;
            *) echo "error: no texture for $name in $view" >&2; exit 1 ;;
        esac
    done
    src="$tmp"
fi

bad=0
for pin in "${PINS[@]}"; do
    name="${pin%% *}" want="${pin##* }"
    f="$src/$name.png"
    if [[ ! -f "$f" ]]; then echo "missing: $f" >&2; bad=1; continue; fi
    [[ "$(sha "$f")" == "$want" ]] || { echo "wrong file (sha256): $f" >&2; bad=1; }
done
[[ $bad == 0 ]] || { echo "nothing installed" >&2; exit 1; }

mkdir -p "$dest/wipes"
for pin in "${PINS[@]}"; do cp "$src/${pin%% *}.png" "$dest/wipes/"; done   # read as wipes/<name>.png
echo "installed ${#PINS[@]} wipe textures in $dest/wipes"

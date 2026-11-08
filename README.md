<div align="center">

<img src=".github/assets/logo.svg" alt="Fate/Slop Order logo" width="120" height="120">

# Fate/Slop Order

**Read the Fate/Grand Order story on your Switch, PC or handheld, with Japanese lookups at the press of a button.**

[![Release](https://img.shields.io/badge/release-v0.1.0-2b4f9e?style=flat-square)](https://github.com/Zureteiru/fate-slop-order/releases/latest)
[![Nintendo Switch](https://img.shields.io/badge/Nintendo%20Switch-homebrew-E60012?style=flat-square&logo=nintendoswitch&logoColor=white)](#nintendo-switch)
[![Windows](https://img.shields.io/badge/Windows-10%2B-0078D4?style=flat-square&logo=windows&logoColor=white)](#windows)
[![PortMaster](https://img.shields.io/badge/PortMaster-aarch64-f2c14e?style=flat-square)](#portmaster-handhelds-anbernic-rg28xx)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue?style=flat-square)](LICENSE)

[**⬇ Download**](https://github.com/Zureteiru/fate-slop-order/releases/latest) · [Setup](#setup-the-files-you-supply) · [Controls](#controls) · [Build from source](#build-from-source) · [How it works](#how-it-works)

</div>

---

Fate/Slop Order plays the main story of Fate/Grand Order the way the game shows it: backgrounds, characters,
expressions, camera moves, wipes, effects, BGM, sound effects and voices, driven by the game's own story
scripts. Point at a word (or press **Y**) and a dictionary popup explains it, so you can read the original
Japanese and look up what you don't know.

- 📖 **Faithful playback:** the game's own scripts, with their fades, moves, waits, branches and choices
- 🔎 **Instant dictionary:** JMdict with Yomitan's deconjugation (行ってしまいました → 行く), ranked by word frequency
- 🗂️ **Chapter picker:** every arc and chapter, read progress, Continue where you stopped
- 💾 **Save anywhere:** an autosave, a quick save and 30 slots
- 📜 **LOG screen, AUTO mode**, English and Japanese scripts
- 🎮 **Runs on** Nintendo Switch (homebrew), Windows, and aarch64 Linux handhelds through PortMaster

> [!IMPORTANT]
> **This project is written primarily by Claude (Anthropic's AI model)**, directed and tested by the
> repository owner. Read the code with that in mind.

> [!NOTE]
> Fate/Slop Order contains **no game assets**. Story scripts, images and audio are downloaded from
> [Atlas Academy](https://atlasacademy.io)'s public game data while you read; the UI art, game font and
> sounds are files you supply yourself (see [Setup](#setup-the-files-you-supply)). It is a fan project, not
> affiliated with or endorsed by TYPE-MOON, Lasengle, Aniplex or Nintendo.

Internally (binaries, folders, code) the project is called **vnreader**: `vnreader.nro`, `vnreader.exe`,
`sdmc:/switch/vnreader/`.

```
core/          Script engine: script → program → player → stage (pure C++17, no OS code)
runtime/       The app: downloads + cache, images, audio mixer, text, picker, saves, dictionary
platform/sdl2/ Draws the frame, reads mouse / touch / keys / controllers, opens the audio device (SDL2)
apps/desktop/  Windows wrapper (os_windows.cpp) and Linux / PortMaster wrapper (os_posix.cpp)
apps/switch/   Nintendo Switch wrapper (libnx, curl, the console's shared font)
third_party/   Vendored libraries: stb, dr_libs, the Safir dictionary module
tools/         Build scripts and installers for the files you supply
```

---

## Download

The [latest release](https://github.com/Zureteiru/fate-slop-order/releases/latest) has:

| File | For |
|---|---|
| `vnreader.nro` | Nintendo Switch (Homebrew Menu) |
| `vnreader-windows-x64.zip` | Windows 10 / 11, 64-bit: `vnreader.exe` and an empty `userdata/` folder |

PortMaster users build the package with one command, see [PortMaster](#portmaster-handhelds-anbernic-rg28xx).

## Setup: the files you supply

Fate/Slop Order keeps everything it needs in one **user folder**:

| Platform | User folder |
|---|---|
| Windows | `userdata\` next to `vnreader.exe` (portable), otherwise `%LOCALAPPDATA%\vnreader` |
| Switch | `sdmc:/switch/vnreader/` |
| PortMaster | `ports/vnreader/userdata/` |

Its contents:

```
userdata/
├── ui/003.png, ui/030.png    the game's story-UI sheets: text box, name tab, choice frame, buttons
├── fonts/FOT-SkipStd-B.otf   the game's dialogue font
├── sfx/choice.mp3            the sound a choice makes
├── wipes/*.png               the 33 wipe masks (circle-in, fades, ...)
├── dict.bin                  the dictionary
├── cache/                    downloaded scripts, images and audio (made by the app)
├── saves/, progress.json     your saves and read progress (made by the app)
└── settings.json             optional volumes: {"volume": {"master": 0.8, "bgm": 1, "se": 1, "voice": 1}}
```

Everything is optional: without the UI sheets the app draws plain boxes, without the game font it uses the
system's Japanese font, without the wipe masks it draws its own wipes, and without `dict.bin` lookups say
"No dictionary". The installers in `tools/` put each file in place (Git Bash on Windows; pass a different
user folder as the last argument):

```bash
bash tools/install_ui_sheets.sh "Fate Grand Order - User Interface.zip"   # Spriters Resource sheet pack
bash tools/install_font.sh "FOT-Skip Std B.otf"                            # the game's font
bash tools/install_choice_sound.sh fgochoice.mp3                           # any MP3 works
bash tools/install_wipes.sh                                                # fetches the 33 masks
bash tools/install_dict.sh JMdict_english.zip jiten_freq_global.zip        # Yomitan dictionary + frequency list
```

The dictionary is any Yomitan-format dictionary (JMdict English recommended), packed into `dict.bin` by
`third_party/switch-vnpop/tools/dict_packer.py` (needs Python 3). With a frequency list such as Jiten's,
entries that tie are ranked the way Hoshi Reader ranks them.

Set it up once on the PC, then copy the whole folder to the Switch or the handheld.

## Nintendo Switch

1. Copy `vnreader.nro` to `sdmc:/switch/`.
2. Copy your user folder's contents to `sdmc:/switch/vnreader/`.
3. Start it from the Homebrew Menu, **through a game's title override** (hold **R** while starting a game)
   so it gets the full memory; the Album applet has much less.

It needs an internet connection for chapters that aren't in `cache/` yet. Downloads use the console's own
TLS certificates. Messages go to `sdmc:/switch/vnreader/vnreader.log` (and to `nxlink -s`).

## Windows

Unzip `vnreader-windows-x64.zip` anywhere and run `vnreader.exe`. Put your files in the `userdata\` folder
beside it. No installer, no runtime to install; Windows 10 or newer.

Command-line options for testing (the app opens the chapter picker without them):

```bash
vnreader.exe --region JP --scripts 0100000010,0100000011     # play these scripts, no picker
vnreader.exe --smoke 14 --screenshot shot.bmp                # hidden window: tap 14 lines, save a frame
vnreader.exe --picker home --screenshot home.bmp             # capture a picker screen
vnreader.exe --no-audio                                       # no sound device
```

The full list is in [apps/README.md](apps/README.md#windows).

## PortMaster handhelds (Anbernic RG28XX)

```bash
bash tools/build_portmaster.sh      # needs Docker; makes build-portmaster/vnreader.zip
```

Unzip `vnreader.zip` into the device's `ports` folder and copy your user folder's contents into
`ports/vnreader/userdata/`. The binary works on any aarch64 firmware with glibc 2.29+ and SDL 2.0.18+
(muOS, Knulli, ROCKNIX, ...). Devices without Wi-Fi, such as the RG28XX, read from `userdata/cache/`
only: read the chapters on the PC first, then copy the cache over.

## Controls

| Input | Action |
|---|---|
| **A** / tap / click / Space / Enter / → | advance (finish the line, then the next one) |
| **B** / right click / Backspace / ← | back one line |
| tap or click a choice / 1–9 | pick a choice |
| **X** / LOG button / `L` | open the LOG (scroll with the d-pad, sticks, wheel or a drag) |
| **−** / AUTO button / `A` key | AUTO on / off |
| **L** / SAVE button / F2 | Save screen |
| **R** / LOAD button / F3 | Load screen |
| F5 / F9 | quick save / quick load |
| **+** / Esc | back to the chapter page; on the picker home, quit |
| `-` / `=` / `M` | volume down / up / mute |

**Dictionary**

| Input | Action |
|---|---|
| **Y** | popup on the first word of the line (in the LOG: the newest entry on screen); **Y** again closes it |
| hold **Shift** over a word (PC) | popup for that word |
| tap a word while the popup is open (Switch) | look that word up |
| d-pad ← → | previous / next word |
| d-pad ↑ ↓ / sticks / wheel | previous / next dictionary entry |
| **A**, **B**, tap outside | close the popup (the story doesn't move) |

The popup shows the reading over the headword, the inflections it undid (-て, -しまう, -ます, -た), the
frequency rank, the parts of speech and the numbered senses.

**Chapter picker:** the d-pad moves between buttons, the sticks or a drag scroll, **A** or a tap picks,
**B** goes up one screen.

## How it works

1. **Catalogue:** the chapter list comes from Fate Café's public reader catalogue, fetched once per launch
   (the cached copy shows at once, so the picker also opens offline).
2. **Scripts:** each part's story scripts are downloaded from Atlas Academy and compiled by `core/` into a
   small program: say a line, show a character, move the camera, wait, branch.
3. **Playback:** the player runs that program against a stage (layers, characters, tweens, wipes, effects),
   and `runtime/` turns the stage into a list of textured quads each frame. SDL2 draws them.
4. **Assets:** images and MP3s come through the same download + disk cache, prefetched for the whole part.
   MP3s are decoded by `dr_mp3` and mixed in software, so audio sounds the same everywhere.
5. **Dictionary:** every drawn character remembers its box on screen. A lookup takes the text from there,
   undoes the inflections with Yomitan's transform table (walked the way Hoshi Reader's hoshidicts does), and
   finds the longest match in `dict.bin` (loaded into memory on the first lookup, about 25 MB for JMdict).
6. **Saves:** a save stores where you are in the script, not the screen. Loading replays the script up to
   that line, muted and without waits, so the stage, music and LOG come back exactly.

Each platform only implements a small interface (downloads, files, log, a font): see
[apps/README.md](apps/README.md) for the details of every target.

## Build from source

| Target | Needs | Command | Output |
|---|---|---|---|
| Windows | Visual Studio 2019+ (C++), CMake 3.16+ | see below | `build-desktop/apps/desktop/Release/vnreader.exe` |
| Switch | Docker | `bash tools/build_switch.sh` | `build-switch/apps/switch/vnreader.nro` |
| PortMaster | Docker | `bash tools/build_portmaster.sh` | `build-portmaster/vnreader.zip` |

```bash
cmake -S . -B build-desktop -G "Visual Studio 16 2019" -A x64 -DVNR_TARGET=desktop -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build-desktop --config Release
```

SDL2 2.32.10 is downloaded and built automatically (pinned by hash). The Switch build runs in the
`devkitpro/devkita64` image (devkitA64, libnx, SDL2 and curl from its portlibs); the PortMaster build
cross-compiles in a Debian bullseye image (`tools/portmaster/Dockerfile`).

## Troubleshooting

- **Plain blue boxes instead of the game's text box:** the UI sheets aren't in `ui/` (the log says
  "no UI sheets").
- **"No dictionary":** `dict.bin` is missing from the user folder.
- **A chapter won't load / the picker shows an error:** no connection and nothing in `cache/` for it. Press
  Retry once you're online, or copy a filled `cache/` from the PC.
- **Switch: the app closes when starting:** start it through title override, not the Album.
- **Anything else:** read `vnreader.log` (Switch), `log.txt` (PortMaster) or the console output (Windows).

## Limits

- Story videos are skipped.
- Choices can't be picked with a controller yet: use the touch screen, a mouse or the number keys. On
  handhelds without a touch screen, choices can't be picked at all for now.
- Events and interludes aren't in the picker yet; the main story is.
- Pitch accent isn't shown in the dictionary popup.
- macOS and Android builds are planned; the code is kept portable for them.

## Credits

- [Atlas Academy](https://atlasacademy.io): the game data the reader downloads
- [Fate Café](https://fatecafe.com): the reader catalogue and the reference for the reader's behaviour
- [JMdict](https://www.edrdg.org/jmdict/j_jmdict.html) (EDRDG), [Jiten](https://jiten.moe) frequency lists,
  [Yomitan](https://github.com/yomidevs/yomitan)'s deconjugation rules, Hoshi Reader's popup layout
- [Safir](https://github.com/jenduis/safir) for the dictionary module
- [SDL2](https://libsdl.org), [stb](https://github.com/nothings/stb), [dr_libs](https://github.com/mackron/dr_libs),
  [devkitPro](https://devkitpro.org), [PortMaster](https://portmaster.games)

## License

[GPL-3.0](LICENSE), because the vendored Safir dictionary module is GPL-3.0. stb and dr_libs are public
domain / MIT-0. Game content is © TYPE-MOON / FGO PROJECT and is not part of this repository.

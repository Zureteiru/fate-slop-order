# vnreader wrappers

All reading logic is shared and platform-free:

| Layer | What it does | Rule |
|---|---|---|
| `core/` | script → Program → Player → stage, text, waits, audio events | pure C++17 |
| `runtime/` | the app: downloads + disk cache, image and MP3 decoding, mixing, fonts and text layout, scene → `DrawList`, input → actions | pure C++17 |
| `platform/sdl2/` | draws a `DrawList`, uploads textures, maps mouse / touch / keys / gamepad to `InputEvent`s, opens the audio device | SDL2 only |
| `apps/<os>/` | the OS services in `runtime/include/vnr/rt/platform.h`, plus packaging | OS-specific code lives only here |

A wrapper is small. It must:

1. **Implement the platform services:**
   - `rt::Http`: async `get` (with optional `Range`) and `poll`;
   - `rt::Files`: read and write under a cache folder;
   - `rt::Log`;
   - optionally `Platform::user`, a second `rt::Files` over the folder the user puts the UI sheets in.
2. **Load a Japanese system font** into `Platform::font`.
3. **Run the loop:**
   1. read the SDL events with `sdl::translate_event`;
   2. call `App::update(dt, input)`;
   3. call `Renderer::sync(app.textures())` and `Renderer::draw(app.draw_list())`;
   4. present.
4. **Open the audio device** (`sdl::AudioDevice`) after the `App` and close it before; its callback only calls
   `App::audio().render()`.

## Status

| Target | Folder | HTTP | Cache folder | User folder (UI sheets) | Font | Status |
|---|---|---|---|---|---|---|
| Windows | `apps/desktop` (`os_windows.cpp`) | WinHTTP | `%LOCALAPPDATA%\vnreader\cache` | `%LOCALAPPDATA%\vnreader` | Yu Gothic / Meiryo | **builds and runs** |
| macOS | `apps/desktop` + `os_posix.cpp` | libcurl (ships with macOS) | `~/Library/Caches/vnreader` | `~/Library/Application Support/vnreader` | Hiragino Sans | later step |
| Switch | `apps/switch` | libcurl (devkitPro portlib) | `sdmc:/switch/vnreader/cache` | `sdmc:/switch/vnreader` | system shared font (`plGetSharedFont`) | later step; SDL2 and curl are in the `devkitpro/devkita64` image |
| Android | `apps/android` (Gradle + SDLActivity) | Java `HttpURLConnection` over JNI | app files dir | app external files dir | `/system/fonts` Noto CJK | later step; needs Android Studio (JDK 17 + NDK) |

## Windows

Needs Visual Studio Build Tools 2019 or newer, and CMake 3.16+. SDL2 2.32.10 is downloaded and built automatically, pinned by hash.

```bash
cmake -S . -B build-desktop -G "Visual Studio 16 2019" -A x64 -DVNR_TARGET=desktop
cmake --build build-desktop --config Release
build-desktop/apps/desktop/Release/vnreader.exe
```

The default is Fuyuki part 1 (NA). Other options:

```bash
vnreader.exe --region JP --scripts 0100000010,0100000011
vnreader.exe --smoke 14 --screenshot shot.bmp   # hidden window: tap 14 lines, save a frame, exit
vnreader.exe --smoke 0 --until "after effect" --screenshot shot.bmp   # stop on the line containing that text
vnreader.exe --smoke 40 --log --screenshot log.bmp   # --log opens the LOG screen, --auto turns AUTO on, before the capture
vnreader.exe --no-audio   # no sound device (the mixer still runs in real time, so voice waits keep their timing)
```

**Controls:**

| Input | Action |
|---|---|
| click / Space / Enter / → / A | advance |
| right click / Backspace / ← / B | back |
| click a choice, or 1–9 | choose |
| click AUTO / `A` key / Y | toggle auto |
| click LOG / `L` / X | open the log |
| in the log: wheel, drag, ↑ ↓ PgUp PgDn, d-pad, sticks | scroll |
| in the log: click, Esc, right click / B, Space / A | close the log |
| `S` | skip to the next script |
| `-` / `=` | master volume down / up (10% steps) |
| `M` | mute / unmute |
| Esc | quit (when the log is closed) |

## Audio

BGM, sound effects and voices play through a software mixer in `runtime/` (`audio_mixer.h`, `audio.h`):

- **Files:** the MP3s come from Atlas through the same download + disk cache as the images, with the fallback URL
  when the first one fails. The whole part is prefetched in the background (4 at a time) once the images are in;
  a cue whose file is not there yet starts when it arrives, except SEs more than 0.5 s late, which are skipped.
  A file that can't be downloaded or decoded is logged and skipped; playback goes on.
- **Decoding:** `dr_mp3` (vendored in `third_party/dr_libs`, unmodified, pure C), streamed from the compressed
  bytes. No OS codecs, so it sounds the same on every target.
- **Buses:** BGM (one track; a new one crossfades, the same one keeps playing), SE (several at once), voice
  (a new line cuts the previous voice). Fades, per-sound volume, loops and stop-all follow the script.
- **Voices:** `[voice]` plays where it appears and is not cut when you tap past its line (it stops at its end,
  on `[voiceStop]`, Back, or the next voice). When it ends (or can't load) the App calls
  `Player::voice_finished()`, which releases `[wait voice]` and lets AUTO move on.
- **Back:** the Player restores that line's BGM; SEs from the later line fade out and old SEs are not replayed.
- **LOG screen:** the story pauses, the sound doesn't.
- **Threads:** the device callback calls `AudioDirector::render()`, which locks the mixer; everything else runs on
  the main loop.
- **Volumes:** `-` / `=` / `M` while reading, or `settings.json` in the user folder (values 0–1):
  `{"volume": {"master": 0.8, "bgm": 1, "se": 1, "voice": 1}}`.
- **Choice click:** picking a choice plays `<user folder>/sfx/choice.mp3` at half SE volume, on its own bus so
  script `soundStopAll` doesn't cut it. The game ships this sound inside the app (Atlas doesn't host it), so
  it is installed like the UI sheets and never committed: `bash tools/install_choice_sound.sh path/to/fgochoice.mp3`
  (the reference copy is Fate Café's `fgochoice.mp3`; the script checks its SHA-256 and accepts any MP3).
  Without it, choices are silent.
- **Movies** are not played yet: they are logged and count as finished at once.

## Game UI art

vnreader draws the story screen with the game's own pieces: the text box, the name tab, the choice
frame and the spinning "next" marker. They come from two sheets of the Spriters Resource
"Fate/Grand Order → Miscellaneous → User Interface" rip (`003.png`, `030.png`). The sheets are game art,
so they are **never committed and never downloaded by the app**. Each user installs them once:

```bash
bash tools/install_ui_sheets.sh "path/to/Mobile - Fate_Grand Order - Miscellaneous - User Interface.zip"
```

The script checks each sheet's SHA-256 (the piece rectangles in `runtime/include/vnr/rt/ui_skin.h` were
measured on those exact files) and copies them to `<user folder>/ui/`. Without them the app draws
plain boxes in the same layout.

The dialogue font is the game's too: FGO uses **FOT-Skip Std B** (Fontworks). It is commercial, so it
is installed the same way and never committed:

```bash
bash tools/install_font.sh "path/to/FOT-Skip Std B.otf"
```

It goes to `<user folder>/fonts/FOT-SkipStd-B.otf`. Without it (or if the file can't be opened) the
app uses the OS's Japanese font.

The layout follows the game, not Fate Café, and was matched against 1080p NA gameplay video to
within 1–2 px (`--smoke 0 --until "text"` captures the frame for a given line):

- **View:** the game shows the top 576 rows of the 1344×626 stage (the rest is background bleed), at
  least the 1024-wide safe area. The window opens at 1024×576; wider windows show more at the sides.
- **Text box:** 984×139 (the sheet piece squashed vertically), 12 px above the bottom of the view.
- **Name tab:** 283×45, drawn over the box's top-left corner, its point sticking out to the left.
- **Text:** FOT-Skip Std B at 28.7 px (names 29 px, choices 28.5 px), two lines 48 apart.
- **Choices:** 777×71 frames, 30 px apart, the dialogue stays visible underneath.
- **LOG and AUTO:** inside the box's right edge, both centred on x 1139.2; LOG's bar at y 446.1 (1.14× the
  sheet piece), AUTO's box at y 486.3 (1.10×). AUTO uses the outlined piece when off and the cyan one when on.
  Taps on them don't advance the text.
- **Log screen:** past lines with speaker names over the dimmed story screen (the story pauses), with a
  scrollbar on the right. It follows the game loosely and wasn't measured.

## Not in the skeleton yet

- **Movies:** movie cues are skipped (logged).
- **Voice replay from the LOG screen:** the backlog keeps each line's voice, but tapping it does nothing yet.
- **Chapter picker:** the part is chosen on the command line instead.
- **Wipes:** wipe patterns are drawn as an even fade.
- **Effects:** particle effects are not drawn.
- **Controller choices:** a controller cannot move between choice buttons yet. Use the 1–9 keys or the mouse.
- **Story buttons:** SKIP and hide-message are not drawn (SKIP is left out on purpose).

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

Without options it opens the **chapter picker** (see below). Other options:

```bash
vnreader.exe --region JP --scripts 0100000010,0100000011   # one part, no picker
vnreader.exe --smoke 14 --screenshot shot.bmp   # hidden window: tap 14 lines, save a frame, exit
vnreader.exe --smoke 0 --until "after effect" --screenshot shot.bmp   # stop on the line containing that text
vnreader.exe --smoke 40 --log --screenshot log.bmp   # --log opens the LOG screen, --auto turns AUTO on, before the capture
vnreader.exe --picker home --screenshot home.bmp   # hidden window: a picker screen once its images are in
vnreader.exe --picker chapter=fuyuki --screenshot ch.bmp   # or end=fuyuki:0 for the end-of-part card
vnreader.exe --scripts 0100000011 --smoke 20 --quicksave --screenshot a.bmp   # quick save at the capture
vnreader.exe --scripts 0100000011 --load quick --smoke 0 --screenshot b.bmp   # load it (auto, quick or 1-30): same frame
vnreader.exe --smoke 20 --savescreen load --screenshot load.bmp   # --savescreen save|load opens that screen before the capture
vnreader.exe --no-audio   # no sound device (the mixer still runs in real time, so voice waits keep their timing)
```

`--smoke` without `--scripts` still plays Fuyuki part 1, so the story smoke runs are unchanged.

**Controls:**

| Input | Action |
|---|---|
| click / Space / Enter / → / A / d-pad → | advance |
| right click / Backspace / ← / B / d-pad ← | back |
| click a choice, or 1–9 | choose |
| click AUTO / `A` key / Y | toggle auto |
| click LOG / `L` / X | open the log |
| in the log: wheel, drag, ↑ ↓ PgUp PgDn, d-pad ↑ ↓, sticks | scroll |
| in the log: click, Esc, right click / B, Space / A | close the log |
| `S` | skip to the next script |
| click SAVE / F2 / L shoulder | Save screen |
| click LOAD / F3 / R shoulder (also the picker home's Load button) | Load screen |
| F5 / F9 | quick save / quick load |
| on the Save / Load screen: d-pad / arrows, A / Enter, tap | move, pick (tabs switch pages) |
| on the Save / Load screen: X / Delete, B / Esc | delete the focused save, close |
| `-` / `=` | master volume down / up (10% steps) |
| `M` | mute / unmute |
| Esc / Start | back to the chapter page (from the picker's home: quit; with `--scripts`: quit) |

## Chapter picker

After Fate Café's reader home (`fatecafe.com/leitor`):

- **Home:** "FGO Reader", an EN/JP switch, a **Start · <first chapter>** button (**Continue · <chapter>** once
  something was opened; it reopens that part), then each arc ("Arc 1 ——— 10 chapters") with a 3-column grid of
  chapter cards: the Atlas banner (`static.atlasacademy.io/NA/Banner/questboard_cap*.png`), the gold label, the
  name and "11 parts · 1h 44min".
- **Chapter page:** "← All chapters", label, name, subtitle, "parts · scenes · time", the banner, **Start: <first
  unread part>**, then "Chapter parts 目次" and one row per part (number, name, "spot · scenes · time", "✓ Read").
  Locked and video-only parts are listed dimmed and can't be picked.
- **End of part:** the part is marked read and a card over the dimmed last frame offers
  **Continue to <next part>** (with its place) and **Back to index**.
- **Reading:** Esc / Start leaves the part for its chapter page (it is not marked read).
- **EN / JP:** EN uses the NA scripts; a part with no English copy plays in JP, as Fate Café disables EN there.

**Input:** d-pad / arrows move the focus between buttons (grid and list alike), the stick / wheel / a drag
scroll (the focus follows), A / Enter / Space or a tap picks, B / Backspace / right click goes up one screen.
The focus logic (`runtime/include/vnr/rt/focus_nav.h`) is generic so the story's choices can use it later.

**Catalogue:** Fate Café's `assets.fatecafe.com/data/atlas/reader.json` (~915 KB, never committed). The app shows
the copy in its download cache at once and fetches a fresh one once per launch in the background; without a
copy and without network it shows an error with Retry. A file the user puts at `<user folder>/catalogue/reader.json`
is used instead and never refreshed (offline use). English names come from its `source*` fields.

**Progress:** `<user folder>/progress.json` (written by the app): the EN/JP choice, the last part opened and the
parts read per chapter. The line inside the part is kept by the autosave (see "Save / Load").

## Save / Load

Save and load anywhere while reading, plus an autosave of the line being read (user decision, 2026-11-07).

- **Slots:** an **autosave**, a **quick save** and **30 slots** (3 pages of 10), in `<user folder>/saves/fgo.json`.
  Runs without the picker (`--scripts`, story smoke runs) use `saves/scripts.json`, so they never replace the
  reader's autosave.
- **Autosave:** every new line or choice replaces it; the file is written at most every 5 s, and always when a part
  is left or started and when the app quits. Finishing a part clears it. The home's **Continue** resumes it when it
  is in the last part opened (same region), otherwise Continue starts that part from its top as before.
- **What a save holds:** the part (chapter, section, region), the script index and id, the stop (instruction index of
  the line or choice), the choices taken earlier in that script, the line number, a hash of the script text, and a
  preview (speaker, text, background, date). Nothing of the stage is stored.
- **Loading** replays that script from its top, muted and without waits, up to the stop (`Player::resume`), so the
  stage, BGM, Back history and LOG are as if the reader had read there. Looping SEs started earlier are not
  restored (same as Back). If Atlas edited the script (the hash differs), it resumes at the first line with the saved
  line number; if that fails too, the script starts from its top (logged). Loading a save from another part switches
  parts.
- **Save / Load screen:** a 2x5 grid of cards (thumbnail = the save's background, slot, date, chapter · part, the
  line) with page tabs; Load also has an "Auto / Quick" page. Overwriting, deleting, and loading from the story
  ask first (loading from the picker does not). The story is paused underneath. Not game art (the game has no save
  screen): it follows the LOG screen's look.
- **SAVE / LOAD buttons:** text plates above the text box's top right, see-through white like LOG
  (`UiLayout::save_button` / `load_button`); they don't move LOG / AUTO.

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

**User folder:** `%LOCALAPPDATA%\vnreader` on Windows. If a folder named `userdata` sits next to the exe or
in one of its parents (e.g. the work folder, when running from `build-desktop\...`), the app uses that
instead, for the cache too, and the install scripts below default to it. It is git-ignored. This
"portable" folder also avoids Windows redirecting writes into a packaged app's private storage (as happens
for anything started from the Claude desktop app).

vnreader draws the story screen with the game's own pieces: the text box, the name tab, the choice
frame and the LOG / AUTO buttons. (The spinning "next" marker on sheet 030 is deliberately not drawn:
the user asked for it to be left out.) They come from two sheets of the Spriters Resource
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

The wipe transitions (`wipein`, `wipeout`, `wipeFilter`) use the game's 33 greyscale wipe masks, as
Fate Café does. They are game art too, so they are installed the same way and never committed:

```bash
bash tools/install_wipes.sh
```

With no argument the script fetches them from fatecafe.com (about 1.6 MB); with a folder it copies
`<name>.png` files from there instead. Each file's SHA-256 is checked, and they go to
`<user folder>/wipes/`. Without them the app draws built-in wipes of the same shapes.

The layout follows the game, not Fate Café. It was matched against 1080p NA gameplay video to
within 1–2 px, then re-checked on 2026-11-07 against a game frame the user supplied (box opacity,
name tab, line pitch, wrap edge, AUTO position). The user confirmed the result, so don't re-measure
without a new reference from them (`--smoke 0 --until "text"` captures the frame for a given line):

- **View:** the game shows the top 576 rows of the 1344×626 stage (the rest is background bleed), at
  least the 1024-wide safe area. The window opens at 1024×576; wider windows show more at the sides.
- **Text box:** 984×139 (the sheet piece squashed vertically), 12 px above the bottom of the view, at
  0.82 opacity (also the name tab and the choice frames; Fate Café uses 82% too).
- **Name tab:** 283×51 (native height) at x 155.5, drawn over the box's top-left corner, its point sticking
  out to the left; the name starts 30.5 in.
- **Text:** FOT-Skip Std B at 28.7 px (names 30.5 px, choices 28.5 px), two lines 48.5 apart. Lines start at
  x 227.5 and wrap at 866 wide (right limit x 1093.5, just short of LOG / AUTO), which reproduces the game's breaks.
- **Choices:** 777×71 frames, 30 px apart, the dialogue stays visible underneath.
- **LOG and AUTO:** inside the box's right edge, both centred on x 1139.2; LOG's bar at y 446.1 (1.14× the
  sheet piece), AUTO's box at y 486.3 (1.10×). AUTO uses the outlined piece when off and the cyan one when on; both boxes share one
  centre, so it doesn't jump when toggled.
  Taps on them don't advance the text.
- **Log screen:** past lines with speaker names over the dimmed story screen (the story pauses), with a
  scrollbar on the right. It follows the game loosely and wasn't measured.

## Not in the skeleton yet

- **Movies:** movie cues are skipped (logged).
- **Voice replay from the LOG screen:** the backlog keeps each line's voice, but tapping it does nothing yet.
- **Picker extras:** Fate Café's Events / Interludes / Story Mode tabs, the map and summary views, search,
  filters and "Mark read" buttons.
- **Sub-camera extras:** split-screen windows (`subCameraFilter maskEdge`) are clipped with a hard edge (the
  script's edge softness is ignored); `subCameraRoll` and `subBlur` are not supported.
- **Controller choices:** a controller cannot move between choice buttons yet (the picker's `FocusNav` is
  meant for it). Use the 1–9 keys or the mouse.
- **Story buttons:** SKIP and hide-message are not drawn (SKIP is left out on purpose).

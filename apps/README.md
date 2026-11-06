# vnreader wrappers

All reading logic is shared and platform-free:

| Layer | What it does | Rule |
|---|---|---|
| `core/` | script → Program → Player → stage, text, waits, audio events | pure C++17 |
| `runtime/` | the app: downloads + disk cache, image decoding, fonts and text layout, scene → `DrawList`, input → actions | pure C++17 |
| `platform/sdl2/` | draws a `DrawList`, uploads textures, maps mouse / touch / keys / gamepad to `InputEvent`s | SDL2 only |
| `apps/<os>/` | the OS services in `runtime/include/vnr/rt/platform.h`, plus packaging | OS-specific code lives only here |

A wrapper is small. It must:

1. **Implement the platform services:**
   - `rt::Http`: async `get` (with optional `Range`) and `poll`;
   - `rt::Files`: read and write under a cache folder;
   - `rt::Log`.
2. **Load a Japanese system font** into `Platform::font`.
3. **Run the loop:**
   1. read the SDL events with `sdl::translate_event`;
   2. call `App::update(dt, input)`;
   3. call `Renderer::sync(app.textures())` and `Renderer::draw(app.draw_list())`;
   4. present.

## Status

| Target | Folder | HTTP | Cache folder | Font | Status |
|---|---|---|---|---|---|
| Windows | `apps/desktop` (`os_windows.cpp`) | WinHTTP | `%LOCALAPPDATA%\vnreader\cache` | Yu Gothic / Meiryo | **builds and runs** |
| macOS | `apps/desktop` + `os_posix.cpp` | libcurl (ships with macOS) | `~/Library/Caches/vnreader` | Hiragino Sans | later step |
| Switch | `apps/switch` | libcurl (devkitPro portlib) | `sdmc:/switch/vnreader/cache` | system shared font (`plGetSharedFont`) | later step; SDL2 and curl are in the `devkitpro/devkita64` image |
| Android | `apps/android` (Gradle + SDLActivity) | Java `HttpURLConnection` over JNI | app files dir | `/system/fonts` Noto CJK | later step; needs Android Studio (JDK 17 + NDK) |

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
```

**Controls:**

| Input | Action |
|---|---|
| click / Space / Enter / → / A | advance |
| right click / Backspace / ← / B | back |
| click a choice, or 1–9 | choose |
| `A` key / Y | toggle auto |
| `S` | skip to the next script |
| Esc | quit |

## Not in the skeleton yet

- **Audio:** the runtime receives the audio events but does not play them.
- **Chapter picker:** the part is chosen on the command line instead.
- **Wipes:** wipe patterns are drawn as an even fade.
- **Effects:** particle effects are not drawn.
- **Controller choices:** a controller cannot move between choice buttons yet. Use the 1–9 keys or the mouse.

# vnreader
Switch homebrew story readers (FGO first, Arknights later)
in the style of the Fate Café reader. Japanese lookups use
the dictionary from third_party/switch-vnpop (core/dict).

Targets: Windows, macOS, Android and Switch. Layers (see apps/README.md):
core/ (script engine) -> runtime/ (shared app logic) -> platform/sdl2/
(drawing + input) -> apps/<os>/ (OS services + packaging).

Rules:
- core/ and runtime/ are pure C++17, no libnx/SDL/OS headers; they build
  and test on PC with -fsanitize=address,undefined.
- SDL code lives only in platform/sdl2/. OS-specific code (Switch,
  Windows, macOS, Android) lives only in apps/*/. Anything that differs
  per OS goes through runtime/include/vnr/rt/platform.h.
- Never edit third_party/; changes go upstream in switch-vnpop.
- Never commit Fate Café source, its assets or game art (UI sheets, fonts, sounds); scratch/ is for downloads.
  Assets taken from Fate Café or the game are installed into the user folder by a
  tools/install_*.sh script with a SHA-256 check (see apps/README.md).
- Work in plan mode first for anything touching core/.
- A task is done only when the test script passes.

Test script: `bash tests/run.sh` (builds and runs the host tests
in the devkitpro/devkita64 Docker image, gcc 12 with ASan+UBSan;
about 30 s, ~15 lines of output). While iterating, run one suite:
`bash tests/run.sh runtime_tests` (or core_tests, dict_tests).
Windows app: see apps/README.md (`-DVNR_TARGET=desktop`, MSVC).

Status (2026-11-07), already done, don't redo or re-check: the core engine, audio, chapter picker,
sub-camera layers and masks, and the whole transitions-and-effects plan (wipes, wipeFilter, pictureFrame,
charaSpecialEffect, roll / roll axis y, gray / sepia / blur filters, hologram scanlines, title cards, named
effects). The story-screen layout was re-measured against a game frame and confirmed by the user. The
portable `userdata/` folder holds all user files. Save / Load anytime with an autosave (30 slots, quick save,
`Player::resume` replay) is done; see apps/README.md "Save / Load". Dictionary lookups (Shift + mouse on PC, Y on
Switch, a Hoshi Reader style popup, `<user folder>/dict.bin` from tools/install_dict.sh) are done; see apps/README.md
"Dictionary". There is no open task: wait for the user to name the
next one rather than picking items from gaps lists yourself.

Player spec: docs/fatecafe_behavior.md. Fate Café is the default: copy its
behaviour, and rip its assets 1:1 when needed. The "vnreader decisions"
section at the top of the spec lists the user's exceptions; those win.

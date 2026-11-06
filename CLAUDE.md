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
- Never commit Fate Café source or game art (UI sheets, fonts); scratch/ is for downloads.
- Work in plan mode first for anything touching core/.
- A task is done only when the test script passes.

Test script: `bash tests/run.sh` (builds and runs the host tests
in the devkitpro/devkita64 Docker image, gcc 12 with ASan+UBSan;
about 30 s, ~15 lines of output). While iterating, run one suite:
`bash tests/run.sh runtime_tests` (or core_tests, dict_tests).
Windows app: see apps/README.md (`-DVNR_TARGET=desktop`, MSVC).

Player spec: docs/fatecafe_behavior.md. The "vnreader decisions"
section at its top overrides Fate Café behaviour: follow the script.

# vnreader
Switch homebrew story readers (FGO first, Arknights later)
in the style of the Fate Café reader. Japanese lookups use
the dictionary from third_party/switch-vnpop (core/dict).

Rules:
- core/ is pure C++17, no libnx/SDL; builds and tests on PC
  with -fsanitize=address,undefined.
- Switch-only code lives in apps/*/.
- Never edit third_party/; changes go upstream in switch-vnpop.
- Never commit Fate Café source; scratch/ is for downloads.
- Work in plan mode first for anything touching core/.
- A task is done only when the test script passes.

Test script: `bash tests/run.sh` (builds and runs the host tests
in the devkitpro/devkita64 Docker image, gcc 12 with ASan+UBSan).

Player spec: docs/fatecafe_behavior.md. The "vnreader decisions"
section at its top overrides Fate Café behaviour: follow the script.

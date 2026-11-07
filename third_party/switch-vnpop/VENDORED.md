# Vendored from jenduis/switch-vnpop

- **Source:** https://github.com/jenduis/switch-vnpop (private)
- **Commit:** `d68b38294c94cd821b12c3c57932ff78723b821a` (2026-11-05)
- **Copied:** the dictionary module only, with paths unchanged.
  - `core/dict/*`, `core/vnpop_ledger.c`
  - `include/vnpop_dict.h`, `vnpop_deinflect.h`, `vnpop_ledger.h`, `vnpop_types.h`
  - `tools/dict_packer.py` (Yomitan JSON → `dict.bin`)
  - `tests/test_dict.cpp`, `test_lookup.cpp`, `test_deinflect_torture.h`
  - `tests/fixtures/test_yomitan_fixture.json`, `test_long_run.json`
- **Not copied:** the full JMdict `dist/dict.bin`, so `test_real_dictionary` reports SKIP.

**Do not edit these files.** Make changes upstream, then re-copy and update the commit hash above.

The project was renamed to Safir (public `jenduis/safir`) on 2026-11-05. If the dictionary moves on there, re-vendor from Safir and update this note.
Checked 2026-11-07: this code is the same as Safir v0.1 (`54f8088`) apart from the vnpop → safir renaming.

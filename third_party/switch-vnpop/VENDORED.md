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

**Do not edit these files**, except for the local patch below. Make changes upstream, then re-copy and update the commit hash above.

The project was renamed to Safir (public `jenduis/safir`) on 2026-11-05. If the dictionary moves on there, re-vendor from Safir and update this note.
Checked 2026-11-07: this code is the same as Safir v0.1 (`54f8088`) apart from the vnpop → safir renaming.

## Local patch (2026-11-07, at the user's request): Yomitan / hoshidicts deconjugation

Not upstream yet. `docs/safir_dictionary_fixes.md` (in vnreader) explains each change so it can be ported to Safir.
Keep it when re-vendoring until Safir has it.
- `tools/yomitan_transforms.json` (new): Yomitan's transform table as hoshidicts e6af33e builds it
  (vnreader `tools/dict_parity/run.sh transforms`). GPL-3.0.
- `tools/gen_deinflect_rules.py` (replaced; the old one was not vendored) generates `core/dict/vnpop_deinflect_rules.h`.
- `core/dict/vnpop_deinflect.c`: hoshidicts' depth-first walk. `include/vnpop_deinflect.h`: Yomitan's conditions
  (uint32), 128-byte traces, 128 candidates, `vnpop_rule_name()`. `include/vnpop_dict.h`: 128-byte rule fields.
- Tests: Yomitan's trace names; 勉強しなければ → 勉強する; 17 torture forms Yomitan does not deinflect were dropped.
- Checked against hoshidicts with vnreader `tools/dict_parity/run.sh deinflect`: 0 differences on 202,522 inputs.
- `tools/dict_packer.py`: frequency rows are read from every input zip (a frequency-only zip such as Jiten ranks
  the others), in every Yomitan shape (`12`, `{"value"}`, `{"reading", "frequency": n | {"value"}}`); the lowest
  rank for the reading or for no reading wins; words the list lacks rank 30000+ by the dictionary's score.

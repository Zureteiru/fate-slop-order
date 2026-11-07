# Safir: dictionary fixes found in vnreader

vnreader vendors Safir's dictionary module (`third_party/switch-vnpop`, which is Safir v0.1 `54f8088` under the
old vnpop names). In November 2026 the deconjugation was changed to work exactly like hoshidicts, the
Yomitan-style backend of Hoshi Reader. This note lists what to change in Safir to get the same result.
Everything here is already done and tested in vnreader's copy, so it can be ported file by file.

**Names:** vnreader's copy uses `vnpop_*` / `Vnpop*` / `VNPOP_*`; Safir uses `safir_*` / `Safir*` / `SAFIR_*`.
The paths inside the module are the same (`core/dict/...`, `include/...`, `tools/...`, `tests/...`).

## 1. What was wrong

The old rule table was written by hand (`tools/gen_deinflect_rules.py`, 361 rules in 40 groups) and walked
breadth-first. Compared with hoshidicts on the same text, it:

- **used its own labels** (`-te`, `-te shimau`, `polite`, `past`, `passive/potential`) instead of Yomitan's
  (`-て`, `-しまう`, `-ます`, `-た`, `potential or passive`). Hoshi Reader shows Yomitan's names.
- **lacked rules Yomitan has**, for example:
  - -ください (起きてください stopped at 起きて)
  - -くる / -いく after -て (帰ってきた only found 帰ってくる, never 帰る)
  - -ちゃう as its own step (食べちゃった came out as -te+-te shimau+past)
  - kansai-ben, slang negatives, -やがる, -ごらん, -いただく, -もらう and more: 72 groups in all
- **had rules Yomitan does not have**, so Hoshi never shows these:
  - noun + する → noun (散歩しています → 散歩 over 7 characters; Yomitan matches 散歩 only)
  - a one-character "stem" result (見せられた also listed 見 → 見る "stem")
  - classical causative -しめる (買わしめる), 行くな, すまい, 美しそうな / 美しそうに, 美しみ
- **walked rules breadth-first with its own stem checks**, so the order of results and which trace was kept
  differed from Yomitan's depth-first walk.

## 2. The fix: Yomitan's own table, walked the way hoshidicts walks it

### 2.1 `tools/yomitan_transforms.json` (new)

This file is Yomitan's Japanese transform table (`ext/js/language/ja/japanese-transforms.js`) exactly as
hoshidicts builds it at commit `e6af33e1ab3241e16b885ec81f048f05d81f766b`: 72 groups, 889 rules, longest
suffix 8 characters. Each rule is `[suffix, replacement, conditions in, conditions out, group]`.

- Copy it from `vnreader/third_party/switch-vnpop/tools/yomitan_transforms.json`. No renaming is needed.
- To regenerate it, run `vnreader/tools/dict_parity/run.sh transforms` (Docker, `gcc:14`). It compiles
  `dump_transforms.cpp` against hoshidicts' `deinflector.cpp` and prints the private rule table.

### 2.2 `tools/gen_deinflect_rules.py` (replace)

The old hand-written generator goes. The new one reads the JSON and writes `core/dict/safir_deinflect_rules.h`:

- `s_rule_names[]`: the group names, in Yomitan's order.
- `s_rules[]`: `{suffix, replacement, suffix bytes, replacement bytes, cond_in, cond_out, group}`, sorted by
  suffix bytes for a binary search. Rules with the same suffix keep Yomitan's order, which decides which of
  two equal results is found first.
- `#define *_DEINFLECT_MAX_SUFFIX_CHARS 8`
- Strings are written as plain UTF-8 literals. MSVC needs `/utf-8`; gcc and clang are fine as they are.

Copy `vnreader/third_party/switch-vnpop/tools/gen_deinflect_rules.py` and replace vnpop with safir in it.

### 2.3 `include/safir_deinflect.h`

- **Conditions:** use Yomitan's, with hoshidicts' bit values, all `uint32_t`:

  `C_V1D 1<<0, C_V1P 1<<1, C_V5D 1<<2, C_V5SS 1<<3, C_V5SP 1<<4, C_VK 1<<5, C_VS 1<<6, C_VZ 1<<7,
  C_ADJI 1<<8, C_MASU 1<<9, C_MASEN 1<<10, C_TE 1<<11, C_BA 1<<12, C_KU 1<<13, C_TA 1<<14, C_NN 1<<15,
  C_NASAI 1<<16, C_YA 1<<17`, plus `C_V1 = V1D|V1P` and `C_V5 = V5D|V5SS|V5SP`.

  `C_SURF` goes: a form with conditions 0 is the surface text, and a rule whose conditions in are 0 applies
  only there.
- **Structs:**
  - `DeinflectRule.cond_in` / `cond_out` become `uint32_t`.
  - The candidate's `conds` becomes `uint32_t`.
  - `rule_trace` becomes `char[128]` (`*_RULE_TRACE_BYTES`). The names are longer now:
    "potential or passive".
- **Limits:**
  - `*_MAX_DEINFLECT_CANDIDATES` goes from 96 to 128. At most 31 came up over 200,000 inputs.
  - `*_DEINFLECT_MAX_STEPS` becomes 16, as a depth guard only.
- **New function:** `const char *safir_rule_name(unsigned id)`.

### 2.4 `core/dict/safir_deinflect.c` (rewrite)

hoshidicts' `Deinflector::deinflect_recursive`, in C:

```
deinflect(text): one character -> only itself; otherwise recurse(text, 0)
recurse(text, conds):
    one character or less -> drop it (no result, no expansion)
    add a result (term, conds, trace)
    for k = min(8, length) down to 1:            # longest suffix first
        for each rule whose suffix is the last k characters, in table order:
            skip unless conds == 0 or (conds & rule.cond_in)
            recurse(text - suffix + rule.replacement, rule.cond_out)
```

- **POS for a result:**
  - `conds == 0` (the surface) → `POS_ALL`
  - V1 → ichidan, V5 → godan, VS or VZ → suru (the packer files vz under suru), VK → kuru, ADJI → adj-i
  - anything else → 0 (an intermediate form such as -て or -ます, never looked up)
- **Trace:** stored base first ("-て+-しまう+-ます+-た"), the order Hoshi Reader shows it in.
- **Duplicates:** a form reached again with the same conditions is not added or expanded a second time.
  This changes no result: the first trace is the one hoshidicts' lookup keeps too.

Copy `vnreader/third_party/switch-vnpop/core/dict/vnpop_deinflect.c` and rename.

### 2.5 `include/safir_dict.h`

`SafirDictMatch.rule` and `SafirLookupResult.deinflect_rule` grow from 48 to 128 bytes. Nothing in
`safir_dict.c` needs to change: it already skips candidates with POS 0 and copies the trace.

### 2.6 Tests

- `tests/test_dict.cpp`: 美しかった's trace is now `"-た"` (was `"past"`).
- `tests/test_lookup.cpp`:
  - 見ていた's trace is `"-て+-いる+-た"` (was `"-te+-te iru+past"`).
  - 勉強しなければ deinflects to 勉強する (was 勉強).
- `tests/test_deinflect_torture.h`: drop the 17 forms Yomitan does not deinflect:
  - 買わしめる, 書かしめる, 泳がしめる, 話さしめる, 待たしめる, 死なしめる, 飛ばしめる, 読ましめる, 走らしめる
  - 食べしめる, 来しめる, せしめる
  - 行くな, すまい, 美しそうな, 美しそうに, 美しみ

  In Safir this header is generated by `tools/gen_deinflect_suite.py`, so drop them there (or filter its
  output through the new deinflector).

### 2.7 Memory (matters on the overlay)

- **Candidate buffer** (static, in `safir_dict.c`): 128 × ~200 B ≈ 25 KB. It was 96 × 128 B ≈ 12 KB.
- **Lookup scratch:** `SafirDictMatch` grows by 80 B, so the 64-entry scratch grows by about 5 KB.
- **Rule table:** about 889 × 32 B (64-bit) plus about 10 KB of strings, roughly 40 KB of read-only data.
- Re-run `tools/check_memory_ledger.py` if the overlay budget counts these.

### 2.8 Licence

Yomitan and hoshidicts are GPL-3.0, like Safir, so the table can be used. Credit Yomitan's
`japanese-transforms.js` and hoshidicts in Safir's README acknowledgements (the generated header and the
generator say where the table comes from).

### 2.9 `tools/dict_packer.py`: frequency ranking (Jiten)

Not part of deconjugation, but it is why 行った showed 行う first. With JMdict alone, 行う outranks 行く
(JMdict's scores say so, in hoshidicts too). Hoshi Reader puts 行く first because of a frequency
dictionary. Both readings are real: 行って is also おこなって, the te-form of 行う, so 行う must stay as a
result, just below 行く. The packer could not use a frequency list:

- It read frequency rows only from the zip being packed, so a separate frequency zip (Jiten's
  `jiten_freq_global.zip`) did nothing.
- It misread Jiten's formats: `{"value": 1, "displayValue": "1㋕"}` gave rank 0, and
  `{"reading": "の", "frequency": {"value": 1, ...}}` raised an error that dropped the whole bank.

The fix:
- `parse_freq()` reads every Yomitan shape: `12`, `{"value": 12}`, `{"reading": r, "frequency": 12}`,
  `{"reading": r, "frequency": {"value": 12}}`.
- `load_freqs()` collects the rows of every input zip first, keeping the lowest rank per (term, reading);
  reading "" applies to every reading.
- `main()` passes them to `load_yomitan_zip()`.
- A term's rank is the lowest of its (term, reading) and (term, "") rows, as hoshidicts matches them.
- With a frequency list, listed words rank below `FREQ_RANK_LIMIT` (30000). Unlisted words get
  30000-32766, ordered by the dictionary's own score, so they come after every listed word (hoshidicts
  treats a missing rank as the largest). Ranks stay below 32768, so the reading-key marker (+32768) still fits.
- The packer accepts several zips: `dict_packer.py JMdict_english.zip jiten_freq_global.zip -o dict.bin`.
- vnreader's popup hides ranks of 30000 and above.

Result: 行ってしまいました → 行く [いく] 50, 行く [ゆく] 50, 行う [おこなう] 674.

## 3. How it was checked

`vnreader/tools/dict_parity/run.sh deinflect` builds hoshidicts' `deinflector.cpp` and vnpop's deinflector
side by side (Docker `gcc:14`) and runs both on 202,522 inputs:

- every substring a lookup tries (1-16 characters from every position) of the JP Fuyuki script
- stems glued to about 100 inflection endings, valid or not

For each input it compares the list of (form, conditions, trace), in order, keeping the first result for each
(form, conditions) as a lookup does. **Result: 0 differences**, and no input came near the candidate cap.
To check Safir, copy `tools/dict_parity/` and point `V=` in `run.sh` at Safir's sources.

## 4. Differences outside deconjugation (seen, not changed)

These came up while comparing with hoshidicts. vnreader left them as they are (the user asked for
deconjugation only; ranking is in 2.9); they are listed in case Safir wants them:

- **Entries with the same headword and reading** stay separate in vnpop (食 [しょく] shows twice). hoshidicts
  merges them into one result with all their glossaries.
- **Text preprocessing:** before deinflecting, hoshidicts also tries these variants of the text:
  - hiragana → katakana (vnpop does only katakana → hiragana)
  - collapsed emphatic sequences (すっっごい)
  - NFKC
  - full-width alphanumerics
  - kanji variants
  - iteration marks (々)
  - numbers → kanji
  - no middle dots
- **Scan window:** vnpop stops at punctuation (`is_break_char`); hoshidicts scans 16 characters regardless.

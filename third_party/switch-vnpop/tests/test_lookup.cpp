/*
 * tests/test_lookup.cpp
 *
 * Chained deinflection, multi-entry lookup order, long same-key runs across index pages,
 * and (when dist/dict.bin is in the checkout) the real JMdict pack on the VN line from
 * the 2026-11-05 console video.
 */
#include "vnpop_dict.h"
#include "vnpop_deinflect.h"
#include "vnpop_ledger.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "[FAIL] Assertion failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

static void make_line(const char *utf8, VnpopTextLine *line) {
    memset(line, 0, sizeof(*line));
    const unsigned char *p = (const unsigned char *)utf8;
    while (*p && line->char_count < VNPOP_MAX_CHARS_PER_LINE) {
        uint32_t cp;
        if (*p < 0x80) { cp = *p; p += 1; }
        else if ((*p & 0xE0) == 0xC0) { cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu); p += 2; }
        else if ((*p & 0xF0) == 0xE0) { cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu); p += 3; }
        else { cp = ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu); p += 4; }
        line->chars[line->char_count++].codepoint = cp;
    }
}

/* Does deinflecting surface give base (as a dictionary-form candidate) with this POS? */
static bool deinflects_to(const char *surface, const char *base, uint16_t pos, std::string *trace = nullptr) {
    static VnpopDeinflectCandidate c[VNPOP_MAX_DEINFLECT_CANDIDATES];
    size_t n = vnpop_deinflect(surface, c, VNPOP_MAX_DEINFLECT_CANDIDATES);
    for (size_t i = 1; i < n; ++i) {
        if (strcmp(c[i].term, base) == 0 && (c[i].pos_mask & pos)) {
            if (trace) *trace = c[i].rule_trace;
            return true;
        }
    }
    return false;
}

static void test_chained_deinflection(void) {
    struct Case { const char *surface, *base; uint16_t pos; } cases[] = {
        { "見ていた", "見る", VNPOP_POS_VERB_ICHIDAN },          /* -te + iru + past */
        { "読んでしまった", "読む", VNPOP_POS_VERB_GODAN },      /* -te + shimau + past */
        { "言われてる", "言う", VNPOP_POS_VERB_GODAN },          /* passive + -te + iru */
        { "行かなきゃ", "行く", VNPOP_POS_VERB_GODAN },          /* -ya + -ba + negative */
        { "書いておいた", "書く", VNPOP_POS_VERB_GODAN },        /* -te + oku + past */
        { "食べさせられなかった", "食べる", VNPOP_POS_VERB_ICHIDAN },
        { "勉強しなければ", "勉強", VNPOP_POS_VERB_SURU },       /* suru noun */
        { "待っていた", "待つ", VNPOP_POS_VERB_GODAN },
        { "美しくなかった", "美しい", VNPOP_POS_ADJ_I },
        { "飲みたくなかった", "飲む", VNPOP_POS_VERB_GODAN },
        { "来なかった", "来る", VNPOP_POS_VERB_KURU },
        { "行った", "行く", VNPOP_POS_VERB_GODAN },              /* irregular te/ta */
        { "とした", "とする", VNPOP_POS_VERB_SURU },
        { "食べちゃった", "食べる", VNPOP_POS_VERB_ICHIDAN },
        { "見られていた", "見る", VNPOP_POS_VERB_ICHIDAN },
        { "書かされた", "書く", VNPOP_POS_VERB_GODAN },          /* causative-passive */
        { "読みながら", "読む", VNPOP_POS_VERB_GODAN },
        { "高すぎる", "高い", VNPOP_POS_ADJ_I },
    };
    for (const Case &c : cases) {
        std::string trace;
        bool ok = deinflects_to(c.surface, c.base, c.pos, &trace);
        if (!ok) fprintf(stderr, "[FAIL] %s did not deinflect to %s\n", c.surface, c.base);
        TEST_ASSERT(ok);
    }
    std::string t;
    TEST_ASSERT(deinflects_to("見ていた", "見る", VNPOP_POS_VERB_ICHIDAN, &t));
    TEST_ASSERT(t == "-te+-te iru+past"); /* base first */

    /* No stem, no word: "た" alone is not a verb, "ない" is not "る". */
    static VnpopDeinflectCandidate c[VNPOP_MAX_DEINFLECT_CANDIDATES];
    size_t n = vnpop_deinflect("た", c, VNPOP_MAX_DEINFLECT_CANDIDATES);
    for (size_t i = 1; i < n; ++i) TEST_ASSERT(strcmp(c[i].term, "る") != 0);
    printf("[PASS] test_chained_deinflection: %zu chains\n", sizeof(cases) / sizeof(cases[0]));
}

/* 150 entries share the key は; the best-ranked one is first, on the run's FIRST index
 * page. The old search looked at the last page starting <= key and the next one only. */
static void test_long_key_run(void) {
    const char *path = "test_long_run.bin";
    FILE *f = fopen(path, "rb");
    if (!f) { printf("[SKIP] test_long_key_run: %s not built\n", path); return; }
    fclose(f);
    vnpop_pool_reset(POOL_DICT_INDEX);
    vnpop_pool_reset(POOL_UI_STATE);
    VnpopDict dict;
    TEST_ASSERT(vnpop_dict_load_file(&dict, path));
    TEST_ASSERT(dict.l1_anchor_count >= 3);
    TEST_ASSERT(dict.header.flags & VNPOP_DICT_FLAG_RANKED);

    VnpopTextLine line;
    make_line("は", &line);
    VnpopLookupResult r;
    TEST_ASSERT(vnpop_dict_scan_longest(&dict, &line, 0, &r));
    TEST_ASSERT(strstr(r.glossary, "topic particle") != nullptr);

    static VnpopDictMatch m[32];
    size_t n = vnpop_dict_lookup_all(&dict, &line, 0, m, 32);
    TEST_ASSERT(n == 32); /* the run has 150: capped */
    char gloss[128];
    TEST_ASSERT(vnpop_dict_read_entry(&dict, &m[0], nullptr, 0, nullptr, 0, gloss, sizeof(gloss)));
    TEST_ASSERT(strstr(gloss, "topic particle") != nullptr);
    for (size_t i = 1; i < n; ++i) TEST_ASSERT(m[i - 1].freq_rank <= m[i].freq_rank);
    vnpop_dict_close(&dict);
    printf("[PASS] test_long_key_run: best entry found across %u index pages\n", (unsigned)dict.header.l1_anchor_count);
}

/* The real JMdict pack, when the checkout has it (it is in git: dist/dict.bin). */
static void test_real_dictionary(void) {
#ifdef VNPOP_SOURCE_DIR
    const char *path = VNPOP_SOURCE_DIR "/dist/dict.bin";
#else
    const char *path = "../dist/dict.bin";
#endif
    FILE *f = fopen(path, "rb");
    if (!f) { printf("[SKIP] test_real_dictionary: %s not present\n", path); return; }
    fclose(f);
    vnpop_pool_reset(POOL_DICT_INDEX);
    vnpop_pool_reset(POOL_UI_STATE);
    static VnpopDict dict;
    TEST_ASSERT(vnpop_dict_load_file(&dict, path));
    static uint8_t cache[4 * VNPOP_L2_PAGE_SIZE];
    vnpop_dict_set_page_cache(&dict, cache, sizeof(cache));

    /* Segmentation of the line from the console video (後には、荒涼とした風が吹くばかりだった。) */
    struct Want { const char *headword; uint16_t chars; } want[] = {
        { "後に", 2 }, { "は", 1 }, { "荒涼", 2 }, { "とする", 3 }, { "風", 1 }, { "が", 1 },
        { "吹く", 2 }, { "ばかり", 3 }, { "だった", 3 },
    };
    VnpopTextLine line;
    make_line("後には、荒涼とした風が吹くばかりだった。", &line);
    uint32_t reads0 = vnpop_dict_page_reads(&dict);
    size_t w = 0;
    for (uint16_t i = 0; i < line.char_count;) {
        VnpopLookupResult r;
        if (!vnpop_dict_scan_longest(&dict, &line, i, &r)) { ++i; continue; }
        TEST_ASSERT(w < sizeof(want) / sizeof(want[0]));
        if (strcmp(r.headword, want[w].headword) != 0 || r.matched_chars != want[w].chars) {
            fprintf(stderr, "[FAIL] word %zu: got %s (%u chars), want %s\n", w, r.headword, r.matched_chars, want[w].headword);
            TEST_ASSERT(false);
        }
        if (w == 1) TEST_ASSERT(strstr(r.glossary, "topic") != nullptr); /* the particle, not 破 */
        ++w;
        i = (uint16_t)(i + r.matched_chars);
    }
    TEST_ASSERT(w == sizeof(want) / sizeof(want[0]));
    uint32_t reads = vnpop_dict_page_reads(&dict) - reads0;
    printf("  real dict: 9 words, %u index page reads with a 4-page cache\n", (unsigned)reads);
    TEST_ASSERT(reads < 80);

    /* Several entries for one word, Yomitan order: 後に first, then the shorter 後 (あと...). */
    static VnpopDictMatch m[32];
    size_t n = vnpop_dict_lookup_all(&dict, &line, 0, m, 32);
    TEST_ASSERT(n >= 4);
    char hw[64], rd[64];
    TEST_ASSERT(vnpop_dict_read_entry(&dict, &m[0], hw, sizeof(hw), rd, sizeof(rd), nullptr, 0));
    TEST_ASSERT(strcmp(hw, "後に") == 0);
    bool has_ato = false;
    for (size_t i = 1; i < n; ++i) {
        TEST_ASSERT(vnpop_dict_read_entry(&dict, &m[i], hw, sizeof(hw), rd, sizeof(rd), nullptr, 0));
        if (strcmp(hw, "後") == 0 && strcmp(rd, "あと") == 0) has_ato = true;
        TEST_ASSERT(m[i - 1].matched_chars >= m[i].matched_chars);
    }
    TEST_ASSERT(has_ato);

    /* Inflected words resolve whole (they used to fall back to a shorter, wrong word). */
    struct Infl { const char *text, *headword; uint16_t chars; } infl[] = {
        { "見ていた", "見る", 4 }, { "読んでしまった", "読む", 7 }, { "言われてる", "言う", 5 },
        { "行かなきゃ", "行く", 5 }, { "書いておいた", "書く", 6 }, { "待っていた", "待つ", 5 },
    };
    for (const Infl &c : infl) {
        make_line(c.text, &line);
        VnpopLookupResult r;
        TEST_ASSERT(vnpop_dict_scan_longest(&dict, &line, 0, &r));
        if (strcmp(r.headword, c.headword) != 0 || r.matched_chars != c.chars) {
            fprintf(stderr, "[FAIL] %s -> %s (%u chars), want %s\n", c.text, r.headword, r.matched_chars, c.headword);
            TEST_ASSERT(false);
        }
    }
    vnpop_dict_close(&dict);
    printf("[PASS] test_real_dictionary\n");
}

void run_lookup_tests(void) {
    printf("\n--- Lookup: chained deinflection, entry lists, real JMdict ---\n");
    test_chained_deinflection();
    test_long_key_run();
    test_real_dictionary();
}

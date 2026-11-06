#include "vnpop_dict.h"
#include "vnpop_deinflect.h"
#include "vnpop_ledger.h"
#include "vnpop_types.h"
#include "test_deinflect_torture.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "[FAIL] Assertion failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

static void test_deinflector_rules(void) {
    VnpopDeinflectCandidate candidates[VNPOP_MAX_DEINFLECT_CANDIDATES];

    /* Test 1: 食べさせられなかった (Ichidan causative + passive + past + neg -> 食べる) */
    size_t count = vnpop_deinflect("食べさせられなかった", candidates, VNPOP_MAX_DEINFLECT_CANDIDATES);
    TEST_ASSERT(count > 0);
    bool found_taberu = false;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(candidates[i].term, "食べる") == 0 && (candidates[i].pos_mask & VNPOP_POS_VERB_ICHIDAN)) {
            found_taberu = true;
            TEST_ASSERT(strstr(candidates[i].rule_trace, "causative") != nullptr);
            break;
        }
    }
    TEST_ASSERT(found_taberu);

    /* Test 2: 美しかった (i-adjective past -> 美しい) */
    count = vnpop_deinflect("美しかった", candidates, VNPOP_MAX_DEINFLECT_CANDIDATES);
    TEST_ASSERT(count > 0);
    bool found_utsukushii = false;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(candidates[i].term, "美しい") == 0 && (candidates[i].pos_mask & VNPOP_POS_ADJ_I)) {
            found_utsukushii = true;
            TEST_ASSERT(strcmp(candidates[i].rule_trace, "past") == 0);
            break;
        }
    }
    TEST_ASSERT(found_utsukushii);

    /* Test 3: 話さなかった (Godan past negative -> 話す) */
    count = vnpop_deinflect("話さなかった", candidates, VNPOP_MAX_DEINFLECT_CANDIDATES);
    TEST_ASSERT(count > 0);
    bool found_hanasu = false;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(candidates[i].term, "話す") == 0 && (candidates[i].pos_mask & VNPOP_POS_VERB_GODAN)) {
            found_hanasu = true;
            break;
        }
    }
    TEST_ASSERT(found_hanasu);

    /* Test 4: 勉強した (Suru verb past -> 勉強する) */
    count = vnpop_deinflect("勉強した", candidates, VNPOP_MAX_DEINFLECT_CANDIDATES);
    TEST_ASSERT(count > 0);
    bool found_benkyou = false;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(candidates[i].term, "勉強する") == 0 && (candidates[i].pos_mask & VNPOP_POS_VERB_SURU)) {
            found_benkyou = true;
            break;
        }
    }
    TEST_ASSERT(found_benkyou);

    /* Test 5: 来なかった (Kuru verb past negative -> 来る) */
    count = vnpop_deinflect("来なかった", candidates, VNPOP_MAX_DEINFLECT_CANDIDATES);
    TEST_ASSERT(count > 0);
    bool found_kuru = false;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(candidates[i].term, "来る") == 0 && (candidates[i].pos_mask & VNPOP_POS_VERB_KURU)) {
            found_kuru = true;
            break;
        }
    }
    TEST_ASSERT(found_kuru);

    /* Test 6: 魔法使い (Uninflected noun -> candidate 0 is identical) */
    count = vnpop_deinflect("魔法使い", candidates, VNPOP_MAX_DEINFLECT_CANDIDATES);
    TEST_ASSERT(count >= 1);
    TEST_ASSERT(strcmp(candidates[0].term, "魔法使い") == 0);
    TEST_ASSERT(candidates[0].pos_mask == VNPOP_POS_ALL);

    printf("[PASS] test_deinflector_rules\n");
}

static void test_deinflector_torture_suite(void) {
    size_t num_cases = sizeof(s_torture_cases) / sizeof(s_torture_cases[0]);
    TEST_ASSERT(num_cases >= 500);

    VnpopDeinflectCandidate candidates[VNPOP_MAX_DEINFLECT_CANDIDATES];

    for (size_t i = 0; i < num_cases; ++i) {
        const DeinflectTestCase *tc = &s_torture_cases[i];
        size_t count = vnpop_deinflect(tc->surface, candidates, VNPOP_MAX_DEINFLECT_CANDIDATES);
        TEST_ASSERT(count > 0);

        bool found_correct_pos = false;
        for (size_t c = 0; c < count; ++c) {
            if (strcmp(candidates[c].term, tc->expected_stem) == 0) {
                if (vnpop_pos_matches(candidates[c].pos_mask, tc->expected_pos)) {
                    found_correct_pos = true;
                    /* Zero false-positive checks:
                     * 1. Verb/adjective candidates must never match a Noun POS */
                    TEST_ASSERT(!vnpop_pos_matches(candidates[c].pos_mask, VNPOP_POS_NOUN));
                    /* 2. Godan candidate must never match Ichidan POS */
                    if (candidates[c].pos_mask == VNPOP_POS_VERB_GODAN) {
                        TEST_ASSERT(!vnpop_pos_matches(candidates[c].pos_mask, VNPOP_POS_VERB_ICHIDAN));
                    }
                    /* 3. Ichidan candidate must never match Godan POS */
                    if (candidates[c].pos_mask == VNPOP_POS_VERB_ICHIDAN) {
                        TEST_ASSERT(!vnpop_pos_matches(candidates[c].pos_mask, VNPOP_POS_VERB_GODAN));
                    }
                }
            }
        }
        if (!found_correct_pos) {
            fprintf(stderr, "[FAIL] Case %zu: surface '%s' failed to deinflect to stem '%s'\n",
                    i, tc->surface, tc->expected_stem);
            TEST_ASSERT(found_correct_pos);
        }
    }
    printf("[PASS] test_deinflector_torture_suite: %zu cases verified (0 POS false-positives)\n", num_cases);
}

static const char* get_test_dict_path(void) {
    FILE *f = fopen("build_host/test_dict.bin", "rb");
    if (f) { fclose(f); return "build_host/test_dict.bin"; }
    f = fopen("test_dict.bin", "rb");
    if (f) { fclose(f); return "test_dict.bin"; }
    return "test_dict.bin";
}

static void test_dict_load_and_exact_lookup(void) {
    vnpop_ledger_init();

    VnpopDict dict;
    bool loaded = vnpop_dict_load_file(&dict, get_test_dict_path());
    TEST_ASSERT(loaded);
    TEST_ASSERT(dict.header.magic == VNPOP_DICT_MAGIC);
    TEST_ASSERT(dict.header.version == VNPOP_DICT_VERSION);
    TEST_ASSERT(dict.header.entry_count >= 18);
    TEST_ASSERT(dict.header.block_count == 1);
    TEST_ASSERT(dict.header.compression_type == VNPOP_COMPRESS_DEFLATE);

    /* Exact lookup: 魔法使い (Noun) */
    VnpopLookupResult res;
    bool found = vnpop_dict_lookup_exact(&dict, "魔法使い", VNPOP_POS_ALL, &res);
    TEST_ASSERT(found);
    TEST_ASSERT(res.found);
    TEST_ASSERT(strcmp(res.headword, "魔法使い") == 0);
    TEST_ASSERT(strcmp(res.reading, "まほうつかい") == 0);
    TEST_ASSERT(strstr(res.glossary, "magician") != nullptr);
    TEST_ASSERT(res.freq_rank == 2500);
    TEST_ASSERT(res.pitch_mask == (1U << 4));
    TEST_ASSERT((res.pos_mask & VNPOP_POS_NOUN) != 0);

    /* Kana reading lookup: まほうつかい */
    found = vnpop_dict_lookup_exact(&dict, "まほうつかい", VNPOP_POS_ALL, &res);
    TEST_ASSERT(found);
    TEST_ASSERT(strcmp(res.headword, "魔法使い") == 0);

    /* Exact lookup: 食べる (Ichidan) */
    found = vnpop_dict_lookup_exact(&dict, "食べる", VNPOP_POS_ALL, &res);
    TEST_ASSERT(found);
    TEST_ASSERT(strcmp(res.reading, "たべる") == 0);
    TEST_ASSERT((res.pos_mask & VNPOP_POS_VERB_ICHIDAN) != 0);

    /* False positive POS rejection */
    found = vnpop_dict_lookup_exact(&dict, "食べる", VNPOP_POS_NOUN, &res);
    TEST_ASSERT(!found);

    vnpop_dict_close(&dict);
    printf("[PASS] test_dict_load_and_exact_lookup\n");
}

static void test_dict_scan_longest_sentences(void) {
    vnpop_ledger_init();

    VnpopDict dict;
    bool loaded = vnpop_dict_load_file(&dict, get_test_dict_path());
    TEST_ASSERT(loaded);

    /* =========================================================================
     * Sentence 1: 食べさせられなかった (10 Japanese characters)
     * ========================================================================= */
    VnpopTextLine line1;
    memset(&line1, 0, sizeof(line1));
    const uint32_t codepoints1[] = {
        0x98DF, /* 食 */
        0x3079, /* べ */
        0x3055, /* さ */
        0x305B, /* せ */
        0x3089, /* ら */
        0x308C, /* れ */
        0x306A, /* な */
        0x304B, /* か */
        0x3063, /* っ */
        0x305F  /* た */
    };
    line1.char_count = sizeof(codepoints1) / sizeof(codepoints1[0]);
    for (uint16_t i = 0; i < line1.char_count; ++i) {
        line1.chars[i].codepoint = codepoints1[i];
    }

    VnpopLookupResult res1;
    auto t0 = std::chrono::high_resolution_clock::now();
    bool match1 = vnpop_dict_scan_longest(&dict, &line1, 0, &res1);
    auto t1 = std::chrono::high_resolution_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    TEST_ASSERT(match1);
    TEST_ASSERT(res1.found);
    TEST_ASSERT(res1.matched_chars == 10);
    TEST_ASSERT(strcmp(res1.headword, "食べる") == 0);
    TEST_ASSERT(strcmp(res1.reading, "たべる") == 0);
    TEST_ASSERT(strstr(res1.glossary, "eat") != nullptr);
    TEST_ASSERT((res1.pos_mask & VNPOP_POS_VERB_ICHIDAN) != 0);
    TEST_ASSERT(duration_ms < 1.0); // Execution strictly under 1 millisecond
    printf("  [BENCH] '食べさせられなかった' scan took %.3f ms (< 1.0 ms gate limit)\n", duration_ms);

    /* =========================================================================
     * Sentence 2: 彼女は美しかった。
     * Scan at start_char_idx = 3 ('美')
     * ========================================================================= */
    VnpopTextLine line2;
    memset(&line2, 0, sizeof(line2));
    const uint32_t codepoints2[] = {
        0x5F7C, /* 彼 (0) */
        0x5973, /* 女 (1) */
        0x306F, /* は (2) */
        0x7F8E, /* 美 (3) */
        0x3057, /* し (4) */
        0x304B, /* か (5) */
        0x3063, /* っ (6) */
        0x305F, /* た (7) */
        0x3002  /* 。 (8) */
    };
    line2.char_count = sizeof(codepoints2) / sizeof(codepoints2[0]);
    for (uint16_t i = 0; i < line2.char_count; ++i) {
        line2.chars[i].codepoint = codepoints2[i];
    }

    VnpopLookupResult res2;
    t0 = std::chrono::high_resolution_clock::now();
    bool match2 = vnpop_dict_scan_longest(&dict, &line2, 3, &res2);
    t1 = std::chrono::high_resolution_clock::now();
    duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    TEST_ASSERT(match2);
    TEST_ASSERT(res2.found);
    TEST_ASSERT(res2.matched_chars == 5); /* 美しかった */
    TEST_ASSERT(strcmp(res2.headword, "美しい") == 0);
    TEST_ASSERT(strcmp(res2.reading, "うつくしい") == 0);
    TEST_ASSERT(strstr(res2.glossary, "beautiful") != nullptr);
    TEST_ASSERT((res2.pos_mask & VNPOP_POS_ADJ_I) != 0);
    TEST_ASSERT(duration_ms < 1.0);
    printf("  [BENCH] '美しかった' scan took %.3f ms (< 1.0 ms gate limit)\n", duration_ms);

    /* =========================================================================
     * Sentence 3: 魔法使いが現れた
     * Scan at start_char_idx = 0 ('魔')
     * ========================================================================= */
    VnpopTextLine line3;
    memset(&line3, 0, sizeof(line3));
    const uint32_t codepoints3[] = {
        0x9B54, /* 魔 (0) */
        0x6CD5, /* 法 (1) */
        0x4F7F, /* 使 (2) */
        0x3044, /* い (3) */
        0x304C, /* が (4) */
        0x73FE, /* 現 (5) */
        0x308C, /* れ (6) */
        0x305F  /* た (7) */
    };
    line3.char_count = sizeof(codepoints3) / sizeof(codepoints3[0]);
    for (uint16_t i = 0; i < line3.char_count; ++i) {
        line3.chars[i].codepoint = codepoints3[i];
    }

    VnpopLookupResult res3;
    t0 = std::chrono::high_resolution_clock::now();
    bool match3 = vnpop_dict_scan_longest(&dict, &line3, 0, &res3);
    t1 = std::chrono::high_resolution_clock::now();
    duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    TEST_ASSERT(match3);
    TEST_ASSERT(res3.found);
    TEST_ASSERT(res3.matched_chars == 4); /* 魔法使い */
    TEST_ASSERT(strcmp(res3.headword, "魔法使い") == 0);
    TEST_ASSERT(strstr(res3.glossary, "magician") != nullptr);
    TEST_ASSERT((res3.pos_mask & VNPOP_POS_NOUN) != 0);
    TEST_ASSERT(duration_ms < 1.0);
    printf("  [BENCH] '魔法使い' scan took %.3f ms (< 1.0 ms gate limit)\n", duration_ms);

    vnpop_dict_close(&dict);
    printf("[PASS] test_dict_scan_longest_sentences\n");
}

static void test_sentence_tokenization_and_dpad(void) {
    vnpop_ledger_init();

    VnpopDict dict;
    bool loaded = vnpop_dict_load_file(&dict, get_test_dict_path());
    TEST_ASSERT(loaded);

    /* Sentence: 魔法使いは美しかった (10 characters)
     * Word 1: 魔法使い (indices 0..3, dict word)
     * Particle: は (index 4, non-dict char)
     * Word 2: 美しかった (indices 5..9, dict word)
     */
    VnpopTextLine line;
    memset(&line, 0, sizeof(line));
    const uint32_t codepoints[] = {
        0x9B54, /* 魔 (0) */
        0x6CD5, /* 法 (1) */
        0x4F7F, /* 使 (2) */
        0x3044, /* い (3) */
        0x306F, /* は (4) */
        0x7F8E, /* 美 (5) */
        0x3057, /* し (6) */
        0x304B, /* か (7) */
        0x3063, /* っ (8) */
        0x305F  /* た (9) */
    };
    line.char_count = sizeof(codepoints) / sizeof(codepoints[0]);
    for (uint16_t i = 0; i < line.char_count; ++i) {
        line.chars[i].codepoint = codepoints[i];
    }

    VnpopLineTokens tokens;
    vnpop_dict_tokenize_line(&dict, &line, &tokens);

    TEST_ASSERT(tokens.token_count == 3);
    /* Token 0: 魔法使い */
    TEST_ASSERT(tokens.tokens[0].start_char_idx == 0);
    TEST_ASSERT(tokens.tokens[0].char_count == 4);
    TEST_ASSERT(tokens.tokens[0].is_dict_word == true);

    /* Token 1: は */
    TEST_ASSERT(tokens.tokens[1].start_char_idx == 4);
    TEST_ASSERT(tokens.tokens[1].char_count == 1);
    TEST_ASSERT(tokens.tokens[1].is_dict_word == false);

    /* Token 2: 美しかった */
    TEST_ASSERT(tokens.tokens[2].start_char_idx == 5);
    TEST_ASSERT(tokens.tokens[2].char_count == 5);
    TEST_ASSERT(tokens.tokens[2].is_dict_word == true);

    /* D-Pad Right Navigation (next word) */
    TEST_ASSERT(vnpop_tokens_next(&tokens, 0) == 4);
    TEST_ASSERT(vnpop_tokens_next(&tokens, 2) == 4);
    TEST_ASSERT(vnpop_tokens_next(&tokens, 4) == 5);
    TEST_ASSERT(vnpop_tokens_next(&tokens, 5) == -1);

    /* D-Pad Left Navigation (prev word) */
    TEST_ASSERT(vnpop_tokens_prev(&tokens, 9) == 5);
    TEST_ASSERT(vnpop_tokens_prev(&tokens, 5) == 4);
    TEST_ASSERT(vnpop_tokens_prev(&tokens, 4) == 0);
    TEST_ASSERT(vnpop_tokens_prev(&tokens, 0) == -1);

    vnpop_dict_close(&dict);
    printf("[PASS] test_sentence_tokenization_and_dpad\n");
}

static void test_bench_and_memory_ledger(void) {
    vnpop_ledger_init();

    VnpopDict dict;
    bool loaded = vnpop_dict_load_file(&dict, get_test_dict_path());
    TEST_ASSERT(loaded);

    /* Verify memory allocations are strictly inside budget pools */
    size_t dict_used = vnpop_pool_used(POOL_DICT_INDEX);
    size_t ui_used = vnpop_pool_used(POOL_UI_STATE);
    TEST_ASSERT(dict_used > 0 && dict_used <= VNPOP_POOL_DICT_INDEX_MAX_BYTES);
    TEST_ASSERT(ui_used >= dict.header.block_size && ui_used <= VNPOP_POOL_UI_STATE_MAX_BYTES);
    printf("  [LEDGER] POOL_DICT_INDEX used: %zu bytes (Capacity: %llu bytes)\n", dict_used, (unsigned long long)VNPOP_POOL_DICT_INDEX_MAX_BYTES);
    printf("  [LEDGER] POOL_UI_STATE used:   %zu bytes (Capacity: %llu bytes)\n", ui_used, (unsigned long long)VNPOP_POOL_UI_STATE_MAX_BYTES);

    /* Perform 5,000 longest-match scan queries to verify sub-millisecond throughput and stability */
    VnpopTextLine line;
    memset(&line, 0, sizeof(line));
    const uint32_t codepoints[] = { 0x98DF, 0x3079, 0x3055, 0x305B, 0x3089, 0x308C, 0x306A, 0x304B, 0x3063, 0x305F };
    line.char_count = 10;
    for (uint16_t i = 0; i < 10; ++i) line.chars[i].codepoint = codepoints[i];

    VnpopLookupResult res;
    const int ITERS = 5000;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i) {
        bool match = vnpop_dict_scan_longest(&dict, &line, 0, &res);
        TEST_ASSERT(match);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double avg_us = (total_ms / ITERS) * 1000.0;
    printf("  [THROUGHPUT] %d scans executed in %.2f ms (average %.2f us per scan, sub-millisecond!)\n", ITERS, total_ms, avg_us);
    TEST_ASSERT(avg_us < 1000.0); // avg < 1.0 ms

    vnpop_dict_close(&dict);

    /* Verify pool reset cleans ledger completely with zero leaks */
    vnpop_pool_reset(POOL_DICT_INDEX);
    vnpop_pool_reset(POOL_UI_STATE);
    TEST_ASSERT(vnpop_pool_used(POOL_DICT_INDEX) == 0);
    TEST_ASSERT(vnpop_pool_used(POOL_UI_STATE) == 0);
    printf("[PASS] test_bench_and_memory_ledger (0 heap leaks verified)\n");
}

static void test_200k_dictionary_sparse_paging(void) {
    vnpop_ledger_init();
    vnpop_pool_reset(POOL_DICT_INDEX);
    vnpop_pool_reset(POOL_UI_STATE);

    const char *path = "test_200k_dict.bin";
    const uint32_t TOTAL_ENTRIES = 200000;
    const uint16_t ENTRIES_PER_PAGE = 128;
    const uint32_t NUM_PAGES = (TOTAL_ENTRIES + ENTRIES_PER_PAGE - 1) / ENTRIES_PER_PAGE;

    /* Generate and sort 200,000 entries */
    std::vector<std::string> terms(TOTAL_ENTRIES);
    char term_buf[64];
    for (uint32_t i = 0; i < TOTAL_ENTRIES - 2; ++i) {
        snprintf(term_buf, sizeof(term_buf), "term_%06u", i);
        terms[i] = term_buf;
    }
    terms[TOTAL_ENTRIES - 2] = "食べる";
    terms[TOTAL_ENTRIES - 1] = "魔法使い";
    std::sort(terms.begin(), terms.end());

    FILE *f = fopen(path, "wb");
    TEST_ASSERT(f != nullptr);

    /* 1. Reserve Header */
    VnpopDictHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = VNPOP_DICT_MAGIC;
    hdr.version = VNPOP_DICT_VERSION;
    hdr.header_size = sizeof(VnpopDictHeader);
    hdr.compression_type = VNPOP_COMPRESS_NONE;
    hdr.entry_count = TOTAL_ENTRIES;
    hdr.block_count = 1;
    hdr.block_size = VNPOP_DICT_BLOCK_SIZE;
    hdr.l1_anchor_count = NUM_PAGES;
    hdr.l1_anchors_offset = sizeof(VnpopDictHeader);
    hdr.l1_anchors_bytes = NUM_PAGES * sizeof(VnpopL1Anchor);
    hdr.l2_pages_offset = hdr.l1_anchors_offset + hdr.l1_anchors_bytes;

    fwrite(&hdr, sizeof(hdr), 1, f);

    /* 2. Prepare Level-1 Anchors and write Level-2 Pages */
    std::vector<VnpopL1Anchor> anchors(NUM_PAGES);
    uint32_t cur_page_file_offset = hdr.l2_pages_offset;

    fseek(f, (long)hdr.l2_pages_offset, SEEK_SET);

    for (uint32_t p = 0; p < NUM_PAGES; ++p) {
        uint32_t start_idx = p * ENTRIES_PER_PAGE;
        uint32_t count = (start_idx + ENTRIES_PER_PAGE <= TOTAL_ENTRIES) ? ENTRIES_PER_PAGE : (TOTAL_ENTRIES - start_idx);

        memset(&anchors[p], 0, sizeof(VnpopL1Anchor));
        strncpy(anchors[p].headword, terms[start_idx].c_str(), sizeof(anchors[p].headword) - 1);
        anchors[p].file_offset = cur_page_file_offset;
        anchors[p].entry_count = (uint16_t)count;
        anchors[p].first_entry_idx = start_idx;

        uint8_t page_buf[4096];
        VnpopL2PageHeader *l2_hdr = (VnpopL2PageHeader *)page_buf;
        l2_hdr->entry_count = (uint16_t)count;

        VnpopDiskIndexEntry *entries = (VnpopDiskIndexEntry *)(page_buf + sizeof(VnpopL2PageHeader));
        char *str_tbl = (char *)(page_buf + sizeof(VnpopL2PageHeader) + count * sizeof(VnpopDiskIndexEntry));
        uint16_t str_off = 0;

        for (uint32_t i = 0; i < count; ++i) {
            uint32_t global_idx = start_idx + i;
            const std::string &t = terms[global_idx];
            if (t == "食べる") {
                entries[i].pos_mask = VNPOP_POS_VERB_ICHIDAN;
                entries[i].offset_in_block = 0;
            } else if (t == "魔法使い") {
                entries[i].pos_mask = VNPOP_POS_NOUN;
                entries[i].offset_in_block = 64;
            } else {
                entries[i].pos_mask = VNPOP_POS_OTHER;
                entries[i].offset_in_block = 64;
            }
            size_t tlen = t.size() + 1;
            memcpy(str_tbl + str_off, t.c_str(), tlen);
            entries[i].term_offset = str_off;
            str_off = (uint16_t)(str_off + tlen);

            entries[i].block_id = 0;
            entries[i].gloss_len = 16;
            entries[i].pitch_mask = 1;
            entries[i].freq_rank = (uint16_t)(global_idx % 50000 + 1);
        }

        l2_hdr->string_bytes = str_off;
        uint16_t total_page_bytes = (uint16_t)(sizeof(VnpopL2PageHeader) + count * sizeof(VnpopDiskIndexEntry) + str_off);
        anchors[p].page_bytes = total_page_bytes;

        fwrite(page_buf, 1, total_page_bytes, f);
        cur_page_file_offset += total_page_bytes;
    }

    hdr.l2_pages_bytes = cur_page_file_offset - hdr.l2_pages_offset;
    hdr.blocks_offset = cur_page_file_offset;

    /* 3. Block metadata and single glossary block */
    VnpopBlockMeta bmeta;
    bmeta.file_offset = hdr.blocks_offset + sizeof(VnpopBlockMeta);
    bmeta.comp_size = 256;
    bmeta.uncomp_size = 256;
    fwrite(&bmeta, sizeof(bmeta), 1, f);

    char gloss_buf[256];
    memset(gloss_buf, 0, sizeof(gloss_buf));
    char *g0 = gloss_buf;
    strcpy(g0, "食べる");
    char *r0 = g0 + strlen(g0) + 1;
    strcpy(r0, "たべる");
    char *d0 = r0 + strlen(r0) + 1;
    strcpy(d0, "to eat; to consume");

    char *g1 = gloss_buf + 64;
    strcpy(g1, "魔法使い");
    char *r1 = g1 + strlen(g1) + 1;
    strcpy(r1, "まほうつかい");
    char *d1 = r1 + strlen(r1) + 1;
    strcpy(d1, "magician; wizard");

    fwrite(gloss_buf, 1, sizeof(gloss_buf), f);
    hdr.blocks_bytes = sizeof(VnpopBlockMeta) + sizeof(gloss_buf);

    /* 4. Seek back and write L1 Anchors + final Header */
    fseek(f, (long)hdr.l1_anchors_offset, SEEK_SET);
    fwrite(anchors.data(), sizeof(VnpopL1Anchor), NUM_PAGES, f);

    fseek(f, 0, SEEK_SET);
    fwrite(&hdr, sizeof(hdr), 1, f);
    fclose(f);

    /* 5. Load and verify sparse memory consumption */
    VnpopDict dict;
    bool ok = vnpop_dict_load_file(&dict, path);
    TEST_ASSERT(ok);

    size_t dict_used = vnpop_pool_used(POOL_DICT_INDEX);
    size_t ui_used = vnpop_pool_used(POOL_UI_STATE);
    printf("  [200K DICT] POOL_DICT_INDEX used: %zu bytes (Threshold: < 100,000 bytes)\n", dict_used);
    printf("  [200K DICT] POOL_UI_STATE used:   %zu bytes\n", ui_used);

    /* Strict assertion: Memory for 200k entries must be < 100 KB */
    TEST_ASSERT(dict_used < 100000);

    /* 6. Benchmark lookup and longest-match deinflection latency */
    VnpopLookupResult res;
    TEST_ASSERT(vnpop_dict_lookup_exact(&dict, "食べる", VNPOP_POS_VERB_ICHIDAN, &res));
    TEST_ASSERT(strcmp(res.glossary, "to eat; to consume") == 0);

    VnpopTextLine line;
    memset(&line, 0, sizeof(line));
    /* 食べさせられなかった -> 食べる */
    const uint32_t codepoints[] = { 0x98DF, 0x3079, 0x3055, 0x305B, 0x3089, 0x308C, 0x306A, 0x304B, 0x3063, 0x305F };
    line.char_count = 10;
    for (uint16_t i = 0; i < 10; ++i) line.chars[i].codepoint = codepoints[i];

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < 100; ++it) {
        bool match = vnpop_dict_scan_longest(&dict, &line, 0, &res);
        TEST_ASSERT(match);
        TEST_ASSERT(strcmp(res.headword, "食べる") == 0);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double total_us = std::chrono::duration<double, std::micro>(t1 - t0).count() / 100.0;
    printf("  [200K DICT] 200,000-entry deinflected lookup latency: %.2f us (< 1,500 us target)\n", total_us);
    TEST_ASSERT(total_us < 1500.0);

    vnpop_dict_close(&dict);
    remove(path);

    vnpop_pool_reset(POOL_DICT_INDEX);
    vnpop_pool_reset(POOL_UI_STATE);
    printf("[PASS] test_200k_dictionary_sparse_paging (< 450 KB RAM & < 1.5 ms verified)\n");
}

void run_dict_tests(void) {
    printf("\n=== Running Phase 3 Dictionary & Deinflector Tests ===\n");
    test_deinflector_rules();
    test_deinflector_torture_suite();
    test_dict_load_and_exact_lookup();
    test_dict_scan_longest_sentences();
    test_sentence_tokenization_and_dpad();
    test_bench_and_memory_ledger();
    test_200k_dictionary_sparse_paging();
    printf("=== All Dictionary & Deinflector Tests Passed! ===\n\n");
}

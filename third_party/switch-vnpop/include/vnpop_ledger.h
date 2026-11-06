#ifndef VNPOP_LEDGER_H
#define VNPOP_LEDGER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Memory Ledger Static Pool Budgets (total 1,180,000 bytes of .bss).
 *
 * POOL_OCR_WEIGHTS holds the whole VOCR6.2_S_w8.vocr blob, which the real recognizer
 * (libs/vocr/vocr.c) reads in place: 819,792 B blob + ~22 KB backend/context state.
 * The earlier 2345-0002 failures happened at 2.25-4.5 MB of .bss; a 584 KB build booted.
 */
#define VNPOP_POOL_OCR_WEIGHTS_MAX_BYTES    860000ULL /* 860 KB: VOCR6.2 blob (819,792 B) + backend/context */
#define VNPOP_POOL_OCR_SCRATCH_MAX_BYTES    160000ULL /* 160 KB: 2x crop (<=57,750 B) + line finder (~17 KB) + VOCR arena (53,856 B) */
#define VNPOP_POOL_DICT_INDEX_MAX_BYTES     100000ULL /* 100 KB: Level-1 sparse anchors + block table when it fits */
#define VNPOP_POOL_UI_STATE_MAX_BYTES        60000ULL /* 60 KB: 32 KB L2 page + 16 KB glossary block */

#define VNPOP_TOTAL_BUDGET_MAX_BYTES       1180000ULL /* 1.18 MB total */

typedef enum VnpopPool {
    POOL_OCR_WEIGHTS = 0,
    POOL_OCR_SCRATCH = 1,
    POOL_DICT_INDEX  = 2,
    POOL_UI_STATE    = 3,
    POOL_COUNT       = 4
} VnpopPool;

typedef struct VnpopPoolStats {
    const char *name;
    size_t capacity;
    size_t used;
    size_t high_watermark;
    size_t alloc_count;
} VnpopPoolStats;

/**
 * Initializes the static memory ledger arenas and resets all tracking offsets.
 */
void vnpop_ledger_init(void);

/**
 * Allocates `bytes` from the specified pool arena with 8-byte alignment.
 * Returns NULL if the allocation exceeds pool capacity or if the pool is invalid.
 */
void *vnpop_alloc(VnpopPool pool, size_t bytes);

/**
 * Resets the allocation offset of the specified pool back to zero.
 */
void vnpop_pool_reset(VnpopPool pool);

/**
 * Releases everything allocated from `pool` after `mark` (a value previously
 * returned by vnpop_pool_used()). Used for temporary per-call allocations.
 */
void vnpop_pool_rewind(VnpopPool pool, size_t mark);

/**
 * Dumps memory pool ledger statistics (used, capacity, high-watermark) to stdout.
 */
void vnpop_ledger_dump_stats(void);

/**
 * Query stats for an individual pool.
 */
bool vnpop_ledger_get_stats(VnpopPool pool, VnpopPoolStats *out_stats);

/**
 * Returns current bytes used in a given pool.
 */
size_t vnpop_pool_used(VnpopPool pool);

/**
 * Returns capacity in bytes for a given pool.
 */
size_t vnpop_pool_capacity(VnpopPool pool);

/**
 * Returns total static bytes allocated across all pools.
 */
size_t vnpop_ledger_total_used(void);

/**
 * Returns total static budget across all pools.
 */
size_t vnpop_ledger_total_capacity(void);

#ifdef __cplusplus
}
#endif

#endif /* VNPOP_LEDGER_H */

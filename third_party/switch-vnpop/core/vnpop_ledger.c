#include "vnpop_ledger.h"
#include <stdio.h>
#include <string.h>

#if defined(__cplusplus)
  #define VNPOP_ALIGNAS(n) alignas(n)
#elif defined(_MSC_VER)
  #define VNPOP_ALIGNAS(n) __declspec(align(n))
#elif defined(__GNUC__) || defined(__clang__)
  #define VNPOP_ALIGNAS(n) __attribute__((aligned(n)))
#else
  #define VNPOP_ALIGNAS(n) _Alignas(n)
#endif

#define VNPOP_ALIGNMENT 16ULL
#define VNPOP_ALIGN_UP(val, align) (((val) + ((align) - 1ULL)) & ~((align) - 1ULL))

typedef struct VnpopPoolArena {
    const char *name;
    uint8_t *buffer;
    size_t capacity;
    size_t used;
    size_t high_watermark;
    size_t alloc_count;
} VnpopPoolArena;

/* Static arenas backing the pools - strictly zero dynamic allocations */
VNPOP_ALIGNAS(16) static uint8_t s_arena_ocr_weights[VNPOP_POOL_OCR_WEIGHTS_MAX_BYTES];
VNPOP_ALIGNAS(16) static uint8_t s_arena_ocr_scratch[VNPOP_POOL_OCR_SCRATCH_MAX_BYTES];
VNPOP_ALIGNAS(16) static uint8_t s_arena_dict_index[VNPOP_POOL_DICT_INDEX_MAX_BYTES];
VNPOP_ALIGNAS(16) static uint8_t s_arena_ui_state[VNPOP_POOL_UI_STATE_MAX_BYTES];

static VnpopPoolArena s_arenas[POOL_COUNT];
static bool s_initialized = false;

static void ensure_arenas_configured(void) {
    s_arenas[POOL_OCR_WEIGHTS].name = "POOL_OCR_WEIGHTS";
    s_arenas[POOL_OCR_WEIGHTS].buffer = s_arena_ocr_weights;
    s_arenas[POOL_OCR_WEIGHTS].capacity = VNPOP_POOL_OCR_WEIGHTS_MAX_BYTES;

    s_arenas[POOL_OCR_SCRATCH].name = "POOL_OCR_SCRATCH";
    s_arenas[POOL_OCR_SCRATCH].buffer = s_arena_ocr_scratch;
    s_arenas[POOL_OCR_SCRATCH].capacity = VNPOP_POOL_OCR_SCRATCH_MAX_BYTES;

    s_arenas[POOL_DICT_INDEX].name = "POOL_DICT_INDEX";
    s_arenas[POOL_DICT_INDEX].buffer = s_arena_dict_index;
    s_arenas[POOL_DICT_INDEX].capacity = VNPOP_POOL_DICT_INDEX_MAX_BYTES;

    s_arenas[POOL_UI_STATE].name = "POOL_UI_STATE";
    s_arenas[POOL_UI_STATE].buffer = s_arena_ui_state;
    s_arenas[POOL_UI_STATE].capacity = VNPOP_POOL_UI_STATE_MAX_BYTES;
}

void vnpop_ledger_init(void) {
    ensure_arenas_configured();
    for (size_t i = 0; i < (size_t)POOL_COUNT; ++i) {
        s_arenas[i].used = 0;
        s_arenas[i].high_watermark = 0;
        s_arenas[i].alloc_count = 0;
    }
    s_initialized = true;
}

void *vnpop_alloc(VnpopPool pool, size_t bytes) {
    if ((int)pool < 0 || pool >= POOL_COUNT || bytes == 0) {
        return NULL;
    }

    if (!s_initialized) {
        vnpop_ledger_init();
    }

    VnpopPoolArena *arena = &s_arenas[pool];
    size_t aligned_offset = VNPOP_ALIGN_UP(arena->used, VNPOP_ALIGNMENT);

    if (aligned_offset > arena->capacity || bytes > arena->capacity - aligned_offset) {
        /* Pool exhausted: strictly fail instead of silently overflowing */
        return NULL;
    }

    void *ptr = &arena->buffer[aligned_offset];
    arena->used = aligned_offset + bytes;
    if (arena->used > arena->high_watermark) {
        arena->high_watermark = arena->used;
    }
    arena->alloc_count++;

    return ptr;
}

void vnpop_pool_reset(VnpopPool pool) {
    if ((int)pool < 0 || pool >= POOL_COUNT) {
        return;
    }
    if (!s_initialized) {
        vnpop_ledger_init();
    }
    s_arenas[pool].used = 0;
}

void vnpop_pool_rewind(VnpopPool pool, size_t mark) {
    if ((int)pool < 0 || pool >= POOL_COUNT) {
        return;
    }
    if (!s_initialized) {
        vnpop_ledger_init();
    }
    if (mark < s_arenas[pool].used) {
        s_arenas[pool].used = mark;
    }
}

bool vnpop_ledger_get_stats(VnpopPool pool, VnpopPoolStats *out_stats) {
    if ((int)pool < 0 || pool >= POOL_COUNT || !out_stats) {
        return false;
    }
    if (!s_initialized) {
        vnpop_ledger_init();
    }
    const VnpopPoolArena *arena = &s_arenas[pool];
    out_stats->name = arena->name;
    out_stats->capacity = arena->capacity;
    out_stats->used = arena->used;
    out_stats->high_watermark = arena->high_watermark;
    out_stats->alloc_count = arena->alloc_count;
    return true;
}

size_t vnpop_pool_used(VnpopPool pool) {
    if ((int)pool < 0 || pool >= POOL_COUNT) {
        return 0;
    }
    if (!s_initialized) {
        vnpop_ledger_init();
    }
    return s_arenas[pool].used;
}

size_t vnpop_pool_capacity(VnpopPool pool) {
    if ((int)pool < 0 || pool >= POOL_COUNT) {
        return 0;
    }
    if (!s_initialized) {
        vnpop_ledger_init();
    }
    return s_arenas[pool].capacity;
}

size_t vnpop_ledger_total_used(void) {
    if (!s_initialized) {
        vnpop_ledger_init();
    }
    size_t total = 0;
    for (size_t i = 0; i < (size_t)POOL_COUNT; ++i) {
        total += s_arenas[i].used;
    }
    return total;
}

size_t vnpop_ledger_total_capacity(void) {
    if (!s_initialized) {
        vnpop_ledger_init();
    }
    size_t total = 0;
    for (size_t i = 0; i < (size_t)POOL_COUNT; ++i) {
        total += s_arenas[i].capacity;
    }
    return total;
}

void vnpop_ledger_dump_stats(void) {
    if (!s_initialized) {
        vnpop_ledger_init();
    }

    printf("======================================================================\n");
    printf(" switch-vnpop Memory Ledger Status (Total Budget: %zu bytes)\n", (size_t)VNPOP_TOTAL_BUDGET_MAX_BYTES);
    printf("----------------------------------------------------------------------\n");
    printf(" %-20s | %10s | %10s | %10s | %5s\n", "Pool Name", "Used", "High-Water", "Capacity", "Alloc");
    printf("----------------------------------------------------------------------\n");

    for (size_t i = 0; i < (size_t)POOL_COUNT; ++i) {
        const VnpopPoolArena *a = &s_arenas[i];
        printf(" %-20s | %10zu | %10zu | %10zu | %5zu\n",
               a->name, a->used, a->high_watermark, a->capacity, a->alloc_count);
    }
    printf("----------------------------------------------------------------------\n");
    printf(" Total Static Used: %zu / %zu bytes (%.2f%% utilized)\n",
           vnpop_ledger_total_used(),
           vnpop_ledger_total_capacity(),
           (double)vnpop_ledger_total_used() * 100.0 / (double)vnpop_ledger_total_capacity());
    printf("======================================================================\n");
}

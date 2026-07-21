/* memory_test.c — unit tests for the memory allocator module.
 *
 * Covers: the allocator dispatchers, the system allocator's alignment and
 * statistics, per-tag accounting, the bump arena (carve/reset/used/realloc),
 * and the debug-mode canary + leak checks. No inference logic — this is the
 * foundation allocator layer only.
 */
#include "memory/memory.h"

#include "hbi_test.h"

#include <stdint.h>
#include <string.h>

static void test_tag_strings(void) {
    for (int i = 0; i < HBI_MEM_TAG_COUNT; ++i) {
        HBI_CHECK(hbi_mem_tag_str((hbi_mem_tag)i) != NULL);
    }
    HBI_CHECK_STR_EQ(hbi_mem_tag_str(HBI_MEM_WEIGHTS), "weights");
}

static void test_system_alloc_free(void) {
    hbi_allocator *a = hbi_allocator_system();
    HBI_CHECK(a != NULL);

    void *p = hbi_alloc(a, 128, 0, HBI_MEM_GENERAL);
    HBI_CHECK(p != NULL);
    memset(p, 0xAB, 128); /* must be fully writable */
    hbi_free(a, p);

    /* free(NULL) is a no-op; alloc(0) returns NULL or a freeable pointer. */
    hbi_free(a, NULL);
}

static void test_system_alignment(void) {
    hbi_allocator *a = hbi_allocator_system();
    const size_t aligns[] = {16, 32, 64, 256, 4096};
    for (size_t i = 0; i < HB_ARRAY_LEN(aligns); ++i) {
        void *p = hbi_alloc(a, 100, aligns[i], HBI_MEM_SCRATCH);
        HBI_CHECK_MSG(p != NULL, "alloc failed at alignment %zu", aligns[i]);
        HBI_CHECK_MSG(((uintptr_t)p % aligns[i]) == 0, "misaligned pointer at alignment %zu",
                      aligns[i]);
        hbi_free(a, p);
    }
    /* Non-power-of-two alignment must be rejected. */
    HBI_CHECK(hbi_alloc(a, 16, 24, HBI_MEM_GENERAL) == NULL);
}

static void test_system_realloc(void) {
    hbi_allocator *a = hbi_allocator_system();

    /* realloc(NULL, n) behaves like alloc. */
    unsigned char *p = hbi_realloc(a, NULL, 32, 0, HBI_MEM_GENERAL);
    HBI_CHECK(p != NULL);
    for (int i = 0; i < 32; ++i) {
        p[i] = (unsigned char)i;
    }

    /* Growing preserves existing bytes. */
    p = hbi_realloc(a, p, 256, 0, HBI_MEM_GENERAL);
    HBI_CHECK(p != NULL);
    int preserved = 1;
    for (int i = 0; i < 32; ++i) {
        if (p[i] != (unsigned char)i) {
            preserved = 0;
        }
    }
    HBI_CHECK(preserved);

    /* realloc(p, 0) frees and returns NULL. */
    HBI_CHECK(hbi_realloc(a, p, 0, 0, HBI_MEM_GENERAL) == NULL);
}

static void test_system_stats(void) {
    hbi_allocator *a = hbi_allocator_system();
    hbi_mem_stats before, after;
    HBI_CHECK_EQ_INT(hbi_allocator_stats(a, &before), HBI_OK);

    void *p = hbi_alloc(a, 1000, 0, HBI_MEM_KV);
    HBI_CHECK(p != NULL);
    HBI_CHECK_EQ_INT(hbi_allocator_stats(a, &after), HBI_OK);

    /* Live bytes and block count rose; the KV tag tally rose by >= 1000. */
    HBI_CHECK(after.live_blocks == before.live_blocks + 1);
    HBI_CHECK(after.live_bytes >= before.live_bytes + 1000);
    HBI_CHECK(after.total_alloc_calls == before.total_alloc_calls + 1);
    HBI_CHECK(after.bytes_by_tag[HBI_MEM_KV] >= before.bytes_by_tag[HBI_MEM_KV] + 1000);
    HBI_CHECK(after.peak_bytes >= after.live_bytes);

    hbi_free(a, p);
    hbi_mem_stats freed;
    HBI_CHECK_EQ_INT(hbi_allocator_stats(a, &freed), HBI_OK);
    HBI_CHECK(freed.live_blocks == before.live_blocks);
    HBI_CHECK(freed.total_free_calls == after.total_free_calls + 1);
}

static void test_arena(void) {
    hbi_arena *arena = NULL;
    HBI_CHECK_EQ_INT(hbi_arena_create(&arena, NULL, 4096), HBI_OK);
    HBI_CHECK(arena != NULL);
    HBI_CHECK_EQ_INT((long long)hbi_arena_capacity(arena), 4096);
    HBI_CHECK_EQ_INT((long long)hbi_arena_used(arena), 0);

    hbi_allocator *a = hbi_arena_allocator(arena);
    void *p1 = hbi_alloc(a, 100, 16, HBI_MEM_SCRATCH);
    HBI_CHECK(p1 != NULL);
    HBI_CHECK(((uintptr_t)p1 % 16) == 0);
    HBI_CHECK(hbi_arena_used(arena) >= 100);

    void *p2 = hbi_alloc(a, 64, 64, HBI_MEM_SCRATCH);
    HBI_CHECK(p2 != NULL);
    HBI_CHECK(((uintptr_t)p2 % 64) == 0);
    HBI_CHECK(p2 != p1);

    /* free() is a no-op on an arena — used does not shrink. */
    size_t used_before_free = hbi_arena_used(arena);
    hbi_free(a, p1);
    HBI_CHECK(hbi_arena_used(arena) == used_before_free);

    /* Exhaustion returns NULL rather than overrunning. */
    HBI_CHECK(hbi_alloc(a, 1u << 20, 0, HBI_MEM_SCRATCH) == NULL);

    /* reset reclaims everything. */
    hbi_arena_reset(arena);
    HBI_CHECK_EQ_INT((long long)hbi_arena_used(arena), 0);
    void *p3 = hbi_alloc(a, 100, 16, HBI_MEM_SCRATCH);
    HBI_CHECK(p3 != NULL);

    hbi_arena_destroy(arena);
}

/* Regression: the arena must align the ABSOLUTE address (base + offset), not the
 * bare offset. The backing block is only naturally aligned (~16 B), so a request
 * for a larger alignment after the cursor has been bumped off a boundary is
 * misaligned iff the fix (aligning base+offset) is absent. Alignments >> the base's
 * natural alignment make this deterministic rather than dependent on malloc luck. */
static void test_arena_large_alignment(void) {
    hbi_arena *arena = NULL;
    HBI_CHECK_EQ_INT(hbi_arena_create(&arena, NULL, 1u << 16), HBI_OK);
    hbi_allocator *a = hbi_arena_allocator(arena);

    /* Bump the cursor to an odd offset so offset-only alignment would diverge from
     * absolute-address alignment. */
    void *seed = hbi_alloc(a, 1, 1, HBI_MEM_SCRATCH);
    HBI_CHECK(seed != NULL);

    const size_t aligns[] = {64, 128, 256, 512, 1024, 4096};
    for (size_t i = 0; i < HB_ARRAY_LEN(aligns); ++i) {
        void *p = hbi_alloc(a, 32, aligns[i], HBI_MEM_SCRATCH);
        HBI_CHECK_MSG(p != NULL, "arena alloc failed at alignment %zu", aligns[i]);
        HBI_CHECK_MSG(((uintptr_t)p % aligns[i]) == 0,
                      "arena returned misaligned pointer at alignment %zu", aligns[i]);
    }

    hbi_arena_destroy(arena);
}

static void test_debug_canaries_and_leaks(void) {
    /* Enable debug mode, confirm a clean alloc/free leaves no leak, then confirm
     * a deliberate leak is detected, then clean it up. */
    hbi_mem_debug_set_enabled(true);
    HBI_CHECK(hbi_mem_debug_enabled());

    hbi_allocator *a = hbi_allocator_system();
    void *p = hbi_alloc(a, 48, 0, HBI_MEM_GENERAL);
    HBI_CHECK(p != NULL);
    /* Writing within bounds must not trip the canary on free. */
    memset(p, 0x5A, 48);
    hbi_free(a, p);
    HBI_CHECK_EQ_INT(hbi_mem_check_leaks(), HBI_OK);

    /* Leak one block: check_leaks must report HBI_ERR_STATE. */
    void *leaked = hbi_alloc(a, 16, 0, HBI_MEM_GENERAL);
    HBI_CHECK(leaked != NULL);
    HBI_CHECK_EQ_INT(hbi_mem_check_leaks(), HBI_ERR_STATE);
    hbi_free(a, leaked); /* clean up so we leave a tidy state */
    HBI_CHECK_EQ_INT(hbi_mem_check_leaks(), HBI_OK);

    hbi_mem_debug_set_enabled(false);
    HBI_CHECK(!hbi_mem_debug_enabled());
}

static void test_identity(void) {
    HBI_CHECK_EQ_INT(hbi_memory_selftest(), HBI_OK);
    HBI_CHECK_STR_EQ(hbi_memory_name(), "memory");
}

int main(void) {
    HBI_TEST_BEGIN("memory");
    HBI_RUN(test_tag_strings);
    HBI_RUN(test_system_alloc_free);
    HBI_RUN(test_system_alignment);
    HBI_RUN(test_system_realloc);
    HBI_RUN(test_system_stats);
    HBI_RUN(test_arena);
    HBI_RUN(test_arena_large_alignment);
    HBI_RUN(test_debug_canaries_and_leaks);
    HBI_RUN(test_identity);
    return HBI_TEST_END();
}

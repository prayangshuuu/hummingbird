#include "stream/stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_memory_manager(void) {
    size_t capacities[HB_TIER_COUNT] = {1024, 4096, 8192, 0};
    hbi_memory_manager_t *mm = hbi_memory_manager_create(capacities);
    assert(mm != NULL);

    // Allocate 512 bytes on Tier 1
    bool ok = hbi_memory_manager_allocate(mm, 1, 512, HB_TIER_1);
    assert(ok);
    (void)ok;
    bool resident = (hbi_memory_manager_get_residency(mm, 1) == HB_RESIDENCY_RESIDENT);
    assert(resident);
    (void)resident;

    // Move to Tier 0
    ok = hbi_memory_manager_move(mm, 1, HB_TIER_0);
    assert(ok);
    (void)ok;

    hbi_memory_manager_destroy(mm);
    printf("[ok] memory_manager\n");
}

static void test_cache_manager(void) {
    hbi_cache_manager_t *cache = hbi_cache_manager_create(HB_CACHE_POLICY_LRU, 4096);
    assert(cache != NULL);

    hbi_cache_manager_insert(cache, 1);
    hbi_cache_manager_insert(cache, 2);

    bool access1 = hbi_cache_manager_access(cache, 1);
    assert(access1 == true);
    (void)access1;
    bool access3 = hbi_cache_manager_access(cache, 3);
    assert(access3 == false);
    (void)access3;

    hbi_block_id_t evicted;
    bool evict_ok = hbi_cache_manager_evict(cache, &evicted);
    assert(evict_ok);
    (void)evict_ok;
    assert(evicted == 2); // 2 is LRU because 1 was accessed

    hbi_cache_manager_destroy(cache);
    printf("[ok] cache_manager\n");
}

static void test_scheduler(void) {
    size_t capacities[HB_TIER_COUNT] = {1024, 1024, 8192, 0};
    hbi_stream_engine_t *engine = hbi_stream_engine_create(capacities, HB_CACHE_POLICY_LRU, 1024);
    assert(engine != NULL);

    hbi_scheduler_t *sched = hbi_scheduler_create(engine);
    assert(sched != NULL);

    bool load_ok = hbi_scheduler_load(sched, 100, 512);
    assert(load_ok);
    (void)load_ok;

    bool load_ok2 = hbi_scheduler_load(sched, 101, 512);
    assert(load_ok2);
    (void)load_ok2;

    // Next load should evict block 100
    bool load_ok3 = hbi_scheduler_load(sched, 102, 512);
    assert(load_ok3);
    (void)load_ok3;

    hbi_scheduler_destroy(sched);
    hbi_stream_engine_destroy(engine);
    printf("[ok] scheduler\n");
}

int main(void) {
    if (hbi_stream_selftest() != HBI_OK) {
        fprintf(stderr, "%s: selftest failed\n", hbi_stream_name());
        return 1;
    }

    test_memory_manager();
    test_cache_manager();
    test_scheduler();

    printf("[ok] %s\n", hbi_stream_name());
    return 0;
}

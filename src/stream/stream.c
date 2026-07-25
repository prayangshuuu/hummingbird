#include "stream/stream.h"

const char *hbi_stream_name(void) {
    return "stream";
}

hbi_status hbi_stream_selftest(void) {
    // Basic test of stream components allocation to verify they are functioning
    size_t capacities[HB_TIER_COUNT] = {1024, 4096, 8192, 0};
    hbi_memory_manager_t *mm = hbi_memory_manager_create(capacities);
    if (!mm)
        return HBI_ERR_INTERNAL;

    hbi_cache_manager_t *cache = hbi_cache_manager_create(HB_CACHE_POLICY_LRU, 4096);
    if (!cache) {
        hbi_memory_manager_destroy(mm);
        return HBI_ERR_INTERNAL;
    }

    hbi_scheduler_t *sched = hbi_scheduler_create(mm, cache);
    if (!sched) {
        hbi_cache_manager_destroy(cache);
        hbi_memory_manager_destroy(mm);
        return HBI_ERR_INTERNAL;
    }

    hbi_scheduler_destroy(sched);
    hbi_cache_manager_destroy(cache);
    hbi_memory_manager_destroy(mm);

    return HBI_OK;
}

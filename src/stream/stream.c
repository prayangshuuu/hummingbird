#include "stream/stream.h"
#include <stdlib.h>

const char *hbi_stream_name(void) {
    return "stream";
}

struct hbi_stream_engine_t {
    hbi_memory_manager_t *mm;
    hbi_cache_manager_t *cache;
};

hbi_stream_engine_t *hbi_stream_engine_create(size_t tier_capacities[HB_TIER_COUNT],
                                              hbi_cache_policy_t policy, size_t cache_capacity) {
    hbi_stream_engine_t *engine = calloc(1, sizeof(hbi_stream_engine_t));
    if (!engine)
        return NULL;

    engine->mm = hbi_memory_manager_create(tier_capacities);
    if (!engine->mm) {
        free(engine);
        return NULL;
    }

    engine->cache = hbi_cache_manager_create(policy, cache_capacity);
    if (!engine->cache) {
        hbi_memory_manager_destroy(engine->mm);
        free(engine);
        return NULL;
    }

    return engine;
}

void hbi_stream_engine_destroy(hbi_stream_engine_t *engine) {
    if (engine) {
        if (engine->cache)
            hbi_cache_manager_destroy(engine->cache);
        if (engine->mm)
            hbi_memory_manager_destroy(engine->mm);
        free(engine);
    }
}

bool hbi_stream_engine_load(hbi_stream_engine_t *engine, hbi_block_id_t id, size_t size) {
    if (!engine)
        return false;

    if (hbi_memory_manager_get_residency(engine->mm, id) != HB_RESIDENCY_EVICTED) {
        hbi_cache_manager_access(engine->cache, id);
        return true;
    }

    if (hbi_memory_manager_allocate(engine->mm, id, size, HB_TIER_1)) {
        hbi_cache_manager_insert(engine->cache, id);
        return true;
    }

    hbi_block_id_t evicted;
    if (hbi_cache_manager_evict(engine->cache, &evicted)) {
        hbi_memory_manager_free(engine->mm, evicted);
        if (hbi_memory_manager_allocate(engine->mm, id, size, HB_TIER_1)) {
            hbi_cache_manager_insert(engine->cache, id);
            return true;
        }
    }
    return false;
}

bool hbi_stream_engine_unload(hbi_stream_engine_t *engine, hbi_block_id_t id) {
    if (!engine)
        return false;
    return hbi_memory_manager_free(engine->mm, id);
}

bool hbi_stream_engine_promote(hbi_stream_engine_t *engine, hbi_block_id_t id) {
    if (!engine)
        return false;
    return hbi_memory_manager_move(engine->mm, id, HB_TIER_0);
}

bool hbi_stream_engine_demote(hbi_stream_engine_t *engine, hbi_block_id_t id) {
    if (!engine)
        return false;
    return hbi_memory_manager_move(engine->mm, id, HB_TIER_1);
}

hbi_status hbi_stream_selftest(void) {
    size_t capacities[HB_TIER_COUNT] = {1024, 4096, 8192, 0};
    hbi_stream_engine_t *engine = hbi_stream_engine_create(capacities, HB_CACHE_POLICY_LRU, 4096);
    if (!engine)
        return HBI_ERR_INTERNAL;

    hbi_scheduler_t *sched = hbi_scheduler_create(engine);
    if (!sched) {
        hbi_stream_engine_destroy(engine);
        return HBI_ERR_INTERNAL;
    }

    hbi_scheduler_destroy(sched);
    hbi_stream_engine_destroy(engine);

    return HBI_OK;
}

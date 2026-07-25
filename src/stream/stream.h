#ifndef HB_STREAM_H
#define HB_STREAM_H

#include "common/common.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *hbi_stream_name(void);
hbi_status hbi_stream_selftest(void);

// Memory Tiers
typedef enum {
    HB_TIER_0 = 0, // VRAM / Pin / Hot
    HB_TIER_1 = 1, // RAM / Main Memory
    HB_TIER_2 = 2, // NVMe / SSD / Disk
    HB_TIER_3 = 3, // Remote / Cold / Archive
    HB_TIER_COUNT
} hbi_memory_tier_t;

// Cache Policies
typedef enum { HB_CACHE_POLICY_LRU, HB_CACHE_POLICY_LFU, HB_CACHE_POLICY_ARC } hbi_cache_policy_t;

// Residency Status
typedef enum {
    HB_RESIDENCY_EVICTED,
    HB_RESIDENCY_IN_TRANSIT,
    HB_RESIDENCY_RESIDENT,
    HB_RESIDENCY_PINNED
} hbi_residency_status_t;

// Statistics and Tracking
typedef struct {
    uint64_t hits;
    uint64_t misses;
    uint64_t promotions;
    uint64_t demotions;
    uint64_t bytes_transferred;
    float temporal_locality_score;
} hbi_locality_stats_t;

// Opaque handles
typedef struct hbi_memory_manager_t hbi_memory_manager_t;
typedef struct hbi_cache_manager_t hbi_cache_manager_t;
typedef struct hbi_scheduler_t hbi_scheduler_t;
typedef struct hbi_prefetch_engine_t hbi_prefetch_engine_t;
typedef struct hbi_locality_tracker_t hbi_locality_tracker_t;

typedef uint64_t hbi_block_id_t;

// Memory Manager
hbi_memory_manager_t *hbi_memory_manager_create(size_t tier_capacities[HB_TIER_COUNT]);
void hbi_memory_manager_destroy(hbi_memory_manager_t *mm);
bool hbi_memory_manager_allocate(hbi_memory_manager_t *mm, hbi_block_id_t id, size_t size,
                                 hbi_memory_tier_t tier);
bool hbi_memory_manager_free(hbi_memory_manager_t *mm, hbi_block_id_t id);
hbi_residency_status_t hbi_memory_manager_get_residency(hbi_memory_manager_t *mm,
                                                        hbi_block_id_t id);
bool hbi_memory_manager_move(hbi_memory_manager_t *mm, hbi_block_id_t id,
                             hbi_memory_tier_t new_tier);

// Cache Manager
hbi_cache_manager_t *hbi_cache_manager_create(hbi_cache_policy_t policy, size_t capacity);
void hbi_cache_manager_destroy(hbi_cache_manager_t *cache);
bool hbi_cache_manager_access(hbi_cache_manager_t *cache, hbi_block_id_t id);
void hbi_cache_manager_insert(hbi_cache_manager_t *cache, hbi_block_id_t id);
bool hbi_cache_manager_evict(hbi_cache_manager_t *cache, hbi_block_id_t *evicted_id);

// Scheduler
hbi_scheduler_t *hbi_scheduler_create(hbi_memory_manager_t *mm, hbi_cache_manager_t *cache);
void hbi_scheduler_destroy(hbi_scheduler_t *sched);
bool hbi_scheduler_load(hbi_scheduler_t *sched, hbi_block_id_t id, size_t size);
bool hbi_scheduler_unload(hbi_scheduler_t *sched, hbi_block_id_t id);
bool hbi_scheduler_promote(hbi_scheduler_t *sched, hbi_block_id_t id);
bool hbi_scheduler_demote(hbi_scheduler_t *sched, hbi_block_id_t id);
bool hbi_scheduler_pin(hbi_scheduler_t *sched, hbi_block_id_t id);
bool hbi_scheduler_unpin(hbi_scheduler_t *sched, hbi_block_id_t id);
bool hbi_scheduler_prefetch(hbi_scheduler_t *sched, hbi_block_id_t id, size_t size);
bool hbi_scheduler_evict(hbi_scheduler_t *sched, hbi_block_id_t id);
void hbi_scheduler_invalidate(hbi_scheduler_t *sched, hbi_block_id_t id);
void hbi_scheduler_sync(hbi_scheduler_t *sched);

// Prefetch Engine
hbi_prefetch_engine_t *hbi_prefetch_engine_create(hbi_scheduler_t *sched);
void hbi_prefetch_engine_destroy(hbi_prefetch_engine_t *prefetch);
void hbi_prefetch_engine_submit(hbi_prefetch_engine_t *prefetch, hbi_block_id_t *sequence,
                                size_t count, size_t size_per_block);

// Locality Tracker
hbi_locality_tracker_t *hbi_locality_tracker_create(void);
void hbi_locality_tracker_destroy(hbi_locality_tracker_t *tracker);
void hbi_locality_tracker_record_hit(hbi_locality_tracker_t *tracker, hbi_block_id_t id);
void hbi_locality_tracker_record_miss(hbi_locality_tracker_t *tracker, hbi_block_id_t id);
void hbi_locality_tracker_record_transfer(hbi_locality_tracker_t *tracker, hbi_block_id_t id,
                                          size_t bytes);
void hbi_locality_tracker_record_promotion(hbi_locality_tracker_t *tracker, hbi_block_id_t id);
void hbi_locality_tracker_record_demotion(hbi_locality_tracker_t *tracker, hbi_block_id_t id);
hbi_locality_stats_t hbi_locality_tracker_get_stats(hbi_locality_tracker_t *tracker);

#ifdef __cplusplus
}
#endif

#endif // HB_STREAM_H

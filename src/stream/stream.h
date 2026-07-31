#ifndef HB_STREAM_H
#define HB_STREAM_H

#include "common/common.h"
#include "tensor/tensor.h"
#include "threadpool/threadpool.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Human-readable module name. */
const char *hbi_stream_name(void);
hbi_status hbi_stream_selftest(void);

// ── Memory Tiers ──
typedef enum {
    HB_TIER_VRAM = 0, // Tier 0: GPU VRAM / Accelerator Memory
    HB_TIER_RAM = 1,  // Tier 1: Pinned RAM / Main Memory
    HB_TIER_DISK = 2, // Tier 2: NVMe / SSD / Disk
    HB_TIER_COUNT
} hbi_memory_tier_t;

// ── Cache Policies ──
typedef enum {
    HB_CACHE_POLICY_LRU,
    HB_CACHE_POLICY_LFU,
    HB_CACHE_POLICY_HYBRID
} hbi_cache_policy_t;

// ── Residency Status ──
typedef enum {
    HB_RESIDENCY_EVICTED,    // Not in RAM/VRAM
    HB_RESIDENCY_IN_TRANSIT, // Being loaded/moved
    HB_RESIDENCY_RESIDENT,   // Ready for use
    HB_RESIDENCY_PINNED      // Locked in memory, cannot be evicted
} hbi_residency_status_t;

// ── Telemetry & Statistics ──
typedef struct {
    uint64_t disk_bytes_read;
    uint64_t vram_bytes_uploaded;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t prefetch_hits;
    uint64_t evictions;
    uint64_t active_in_transit;
} hbi_stream_stats_t;

// ── Opaque Handles ──
typedef struct hbi_stream_engine hbi_stream_engine;
typedef struct hbi_stream_block hbi_stream_block;

// ── Block Description ──
struct hbi_format_handler;
struct hbi_tensor_entry;

typedef struct hbi_stream_block_desc {
    uint32_t tensor_id;
    const char *file_path;
    const struct hbi_format_handler *handler;
    const struct hbi_tensor_entry *entry;
    hbi_dtype dtype;
    hbi_shape shape;
    hbi_residency_status_t initial_status;
    hbi_memory_tier_t initial_tier;
} hbi_stream_block_desc;

// ── Engine Lifecycle ──
hbi_status hbi_stream_engine_create(size_t ram_capacity, size_t vram_capacity,
                                    hbi_cache_policy_t policy, hbi_threadpool *io_pool,
                                    hbi_stream_engine **out_engine);
void hbi_stream_engine_destroy(hbi_stream_engine *engine);

// ── Block Registration ──
/* Register a tensor block with the streaming engine. Does not load it. */
hbi_status hbi_stream_register_block(hbi_stream_engine *engine, const hbi_stream_block_desc *desc,
                                     hbi_stream_block **out_block);

hbi_stream_block *hbi_stream_get_block(hbi_stream_engine *engine, uint32_t tensor_id);

/* Get a registered block by tensor_id. Returns NULL if not found. */
hbi_stream_block *hbi_stream_get_block(hbi_stream_engine *engine, uint32_t tensor_id);

// ── Block Operations ──
/* Request a block to be loaded asynchronously into the target tier. */
hbi_status hbi_stream_prefetch(hbi_stream_engine *engine, hbi_stream_block *block,
                               hbi_memory_tier_t target_tier);

/* Block the current thread until the block is fully resident in the target tier. Sets *out_tensor
 * to point to the resident data. */
hbi_status hbi_stream_await(hbi_stream_engine *engine, hbi_stream_block *block,
                            hbi_memory_tier_t target_tier, hbi_tensor *out_tensor);

/* Pin a block so it won't be evicted. */
hbi_status hbi_stream_pin(hbi_stream_engine *engine, hbi_stream_block *block);

/* Unpin a block. */
hbi_status hbi_stream_unpin(hbi_stream_engine *engine, hbi_stream_block *block);

/* Hint to evict the block from RAM/VRAM if space is needed. */
hbi_status hbi_stream_evict(hbi_stream_engine *engine, hbi_stream_block *block);

// ── Telemetry ──
hbi_stream_stats_t hbi_stream_get_stats(hbi_stream_engine *engine);

#ifdef __cplusplus
}
#endif

#endif // HB_STREAM_H

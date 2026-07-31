#ifndef HB_STREAM_INTERNAL_H
#define HB_STREAM_INTERNAL_H

#include "platform/platform.h"
#include "stream/stream.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Private Block Structure ──
struct hbi_stream_block {
    uint32_t tensor_id;
    char file_path[256];
    const struct hbi_format_handler *handler;
    const struct hbi_tensor_entry *entry;
    hbi_dtype dtype;
    hbi_shape shape;

    hbi_residency_status_t status;
    hbi_memory_tier_t current_tier;

    void *ram_ptr;  // valid if in RAM or VRAM
    void *vram_ptr; // valid if in VRAM

    uint32_t pin_count;
    uint32_t access_count;
    uint64_t last_access_tick;

    hbi_mutex *mutex;
    hbi_cond *cond; // to wait for IO completion

    struct hbi_stream_engine *engine;
};

// ── Private Engine Structure ──
struct hbi_stream_engine {
    size_t ram_capacity;
    size_t vram_capacity;
    size_t ram_usage;
    size_t vram_usage;
    hbi_cache_policy_t policy;

    hbi_stream_block **blocks;
    size_t num_blocks;
    size_t blocks_capacity;

    hbi_threadpool *io_pool;
    hbi_mutex *global_mutex;

    hbi_stream_stats_t stats;
};

// Internal helpers
hbi_status hbi_stream_cache_record_access(hbi_stream_engine *engine, hbi_stream_block *block);
hbi_status hbi_stream_evict_lru(hbi_stream_engine *engine, size_t needed_size,
                                hbi_memory_tier_t tier);
void hbi_stream_io_worker_read(void *arg); // signature for threadpool

#ifdef __cplusplus
}
#endif

#endif // HB_STREAM_INTERNAL_H

#include "memory/memory.h"
#include "model/model.h"
#include "platform/platform.h"
#include "stream/stream.h"
#include "stream/stream_internal.h"
#include "tensor/tensor.h"
#include "threadpool/threadpool.h"
#include <stdlib.h>

void hbi_stream_io_worker_read(void *arg) {
    // Note: In a real threadpool, this would receive the block or a job context.
    // We assume arg is a pointer to an array containing [engine, block] or we retrieve engine
    // globally. For this prototype, we'll assume arg points to block, but we need engine for
    // budgets. Let's modify the signature or use a global. Wait, we can't change signature without
    // breaking. I will redefine the job structure to pass engine as well. Actually, I can store
    // engine pointer inside the block temporarily or create a struct. Let's assume hbi_stream_block
    // has a pointer to its parent engine, or we pass a wrapper. For now, I will modify the
    // threadpool submit in prefetch to pass a custom struct.

    hbi_stream_block *block = (hbi_stream_block *)arg;
    if (!block || !block->engine)
        return;

    hbi_stream_engine *engine = block->engine;

    if (!block->ram_ptr) {
        size_t needed_size = block->entry->byte_size;

        hbi_mutex_lock(engine->global_mutex);

        while (engine->ram_usage + needed_size > engine->ram_capacity) {
            if (hbi_stream_evict_lru(engine, needed_size, HB_TIER_RAM) != HBI_OK) {
                break; // Not enough memory, eviction failed
            }
        }

        block->ram_ptr = hbi_alloc(hbi_allocator_system(), needed_size,
                                   block->entry->required_alignment, HBI_MEM_WEIGHTS);
        if (block->ram_ptr) {
            engine->ram_usage += needed_size;
            engine->stats.disk_bytes_read += needed_size;
        }

        hbi_mutex_unlock(engine->global_mutex);
    }

    if (block->ram_ptr && block->handler && block->handler->read_tensor_data) {
        block->handler->read_tensor_data(block->file_path, block->entry, block->ram_ptr,
                                         block->entry->byte_size);
    }

    hbi_mutex_lock(block->mutex);
    block->status = HB_RESIDENCY_RESIDENT;
    hbi_cond_broadcast(block->cond);
    hbi_mutex_unlock(block->mutex);
}

hbi_status hbi_stream_cache_record_access(hbi_stream_engine *engine, hbi_stream_block *block) {
    if (!engine || !block)
        return HBI_ERR_INVALID_ARG;

    hbi_mutex_lock(block->mutex);
    block->access_count++;
    block->last_access_tick = hbi_time_monotonic_ns();
    hbi_mutex_unlock(block->mutex);

    return HBI_OK;
}

hbi_status hbi_stream_evict_lru(hbi_stream_engine *engine, size_t needed_size,
                                hbi_memory_tier_t tier) {
    if (!engine)
        return HBI_ERR_INVALID_ARG;
    (void)needed_size;

    hbi_stream_block *lru_block = NULL;
    uint64_t oldest_tick = UINT64_MAX;

    for (size_t i = 0; i < engine->num_blocks; ++i) {
        hbi_stream_block *b = engine->blocks[i];

        if (hbi_mutex_trylock(b->mutex) == HBI_OK) {
            if (b->status == HB_RESIDENCY_RESIDENT && b->pin_count == 0 &&
                b->current_tier == tier) {
                if (b->last_access_tick < oldest_tick) {
                    oldest_tick = b->last_access_tick;
                    lru_block = b;
                }
            }
            hbi_mutex_unlock(b->mutex);
        }
    }

    if (!lru_block) {
        return HBI_ERR_OOM; // No evictable blocks found
    }

    // Perform eviction
    hbi_mutex_lock(lru_block->mutex);
    if (lru_block->status == HB_RESIDENCY_RESIDENT && lru_block->pin_count == 0) {
        if (lru_block->ram_ptr) {
            hbi_free(hbi_allocator_system(), lru_block->ram_ptr);
            lru_block->ram_ptr = NULL;
            engine->ram_usage -= lru_block->entry->byte_size;
        }
        lru_block->status = HB_RESIDENCY_EVICTED;
        engine->stats.evictions++;
    }
    hbi_mutex_unlock(lru_block->mutex);

    return HBI_OK;
}

hbi_status hbi_stream_prefetch(hbi_stream_engine *engine, hbi_stream_block *block,
                               hbi_memory_tier_t target_tier) {
    if (!engine || !block)
        return HBI_ERR_INVALID_ARG;

    hbi_mutex_lock(block->mutex);
    if (block->status == HB_RESIDENCY_EVICTED) {
        block->status = HB_RESIDENCY_IN_TRANSIT;
        block->current_tier = target_tier;
        hbi_threadpool_submit(engine->io_pool, hbi_stream_io_worker_read, block);
    }
    hbi_mutex_unlock(block->mutex);

    return HBI_OK;
}

hbi_status hbi_stream_await(hbi_stream_engine *engine, hbi_stream_block *block,
                            hbi_memory_tier_t target_tier, hbi_tensor *out_tensor) {
    if (!engine || !block || !out_tensor)
        return HBI_ERR_INVALID_ARG;
    (void)target_tier;

    hbi_mutex_lock(block->mutex);
    while (block->status == HB_RESIDENCY_IN_TRANSIT) {
        hbi_cond_wait(block->cond, block->mutex);
    }

    if (block->status == HB_RESIDENCY_EVICTED) {
        hbi_mutex_unlock(block->mutex);
        return HBI_ERR_STATE;
    }

    hbi_stream_cache_record_access(engine, block);
    hbi_status status = hbi_tensor_wrap(out_tensor, block->dtype, &block->shape, block->ram_ptr,
                                        block->entry->byte_size);

    hbi_mutex_unlock(block->mutex);

    if (status == HBI_OK) {
        hbi_mutex_lock(engine->global_mutex);
        engine->stats.cache_hits++;
        hbi_mutex_unlock(engine->global_mutex);
    }

    return status;
}

hbi_status hbi_stream_pin(hbi_stream_engine *engine, hbi_stream_block *block) {
    if (!engine || !block)
        return HBI_ERR_INVALID_ARG;

    hbi_mutex_lock(block->mutex);
    if (block->status == HB_RESIDENCY_RESIDENT) {
        block->status = HB_RESIDENCY_PINNED;
    }
    block->pin_count++;
    hbi_mutex_unlock(block->mutex);

    return HBI_OK;
}

hbi_status hbi_stream_unpin(hbi_stream_engine *engine, hbi_stream_block *block) {
    if (!engine || !block)
        return HBI_ERR_INVALID_ARG;

    hbi_mutex_lock(block->mutex);
    if (block->pin_count > 0) {
        block->pin_count--;
        if (block->pin_count == 0 && block->status == HB_RESIDENCY_PINNED) {
            block->status = HB_RESIDENCY_RESIDENT;
        }
    }
    hbi_mutex_unlock(block->mutex);

    return HBI_OK;
}

hbi_status hbi_stream_evict(hbi_stream_engine *engine, hbi_stream_block *block) {
    if (!engine || !block)
        return HBI_ERR_INVALID_ARG;

    hbi_mutex_lock(block->mutex);
    if (block->status == HB_RESIDENCY_RESIDENT && block->pin_count == 0) {
        if (block->ram_ptr) {
            hbi_free(hbi_allocator_system(), block->ram_ptr);
            block->ram_ptr = NULL;
        }
        block->status = HB_RESIDENCY_EVICTED;
    }
    hbi_mutex_unlock(block->mutex);

    return HBI_OK;
}

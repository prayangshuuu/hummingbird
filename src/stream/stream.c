#include "stream/stream.h"
#include "common/common.h"
#include "platform/platform.h"
#include "stream/stream_internal.h"
#include <stdlib.h>
#include <string.h>

const char *hbi_stream_name(void) {
    return "stream";
}

hbi_status hbi_stream_selftest(void) {
    return HBI_OK;
}

hbi_status hbi_stream_engine_create(size_t ram_capacity, size_t vram_capacity,
                                    hbi_cache_policy_t policy, hbi_threadpool *io_pool,
                                    hbi_stream_engine **out_engine) {
    if (!out_engine) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "out_engine is NULL");
    }

    hbi_stream_engine *engine = (hbi_stream_engine *)calloc(1, sizeof(hbi_stream_engine));
    if (!engine) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "failed to allocate engine");
    }

    engine->ram_capacity = ram_capacity;
    engine->vram_capacity = vram_capacity;
    engine->ram_usage = 0;
    engine->vram_usage = 0;
    engine->policy = policy;
    engine->io_pool = io_pool;

    engine->blocks_capacity = 64;
    engine->blocks =
        (hbi_stream_block **)calloc(engine->blocks_capacity, sizeof(hbi_stream_block *));
    if (!engine->blocks) {
        free(engine);
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "failed to allocate blocks array");
    }

    hbi_status status = hbi_mutex_init(&engine->global_mutex);
    if (status != HBI_OK) {
        free(engine->blocks);
        free(engine);
        return status;
    }

    memset(&engine->stats, 0, sizeof(engine->stats));

    *out_engine = engine;
    return HBI_OK;
}

void hbi_stream_engine_destroy(hbi_stream_engine *engine) {
    if (!engine) {
        return;
    }

    if (engine->global_mutex) {
        hbi_mutex_destroy(engine->global_mutex);
    }

    if (engine->blocks) {
        for (size_t i = 0; i < engine->num_blocks; ++i) {
            hbi_stream_block *block = engine->blocks[i];
            if (block) {
                if (block->mutex) {
                    hbi_mutex_destroy(block->mutex);
                }
                if (block->cond) {
                    hbi_cond_destroy(block->cond);
                }
                if (block->ram_ptr) {
                    hbi_aligned_free(block->ram_ptr);
                }
                free(block);
            }
        }
        free(engine->blocks);
    }

    free(engine);
}

hbi_status hbi_stream_register_block(hbi_stream_engine *engine, const hbi_stream_block_desc *desc,
                                     hbi_stream_block **out_block) {
    if (!engine || !desc) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "engine or desc is NULL");
    }

    hbi_stream_block *block = (hbi_stream_block *)calloc(1, sizeof(hbi_stream_block));
    if (!block) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "failed to allocate block");
    }

    block->tensor_id = desc->tensor_id;
    if (desc->file_path) {
        strncpy(block->file_path, desc->file_path, sizeof(block->file_path) - 1);
        block->file_path[sizeof(block->file_path) - 1] = '\0';
    }
    block->handler = desc->handler;
    block->entry = desc->entry;
    block->dtype = desc->dtype;
    block->shape = desc->shape;
    block->status = desc->initial_status;
    block->current_tier = desc->initial_tier;
    block->pin_count = 0;
    block->access_count = 0;
    block->last_access_tick = 0;
    block->engine = engine;

    hbi_status status = hbi_mutex_init(&block->mutex);
    if (status != HBI_OK) {
        free(block);
        return status;
    }

    status = hbi_cond_init(&block->cond);
    if (status != HBI_OK) {
        hbi_mutex_destroy(block->mutex);
        free(block);
        return status;
    }

    hbi_mutex_lock(engine->global_mutex);

    if (engine->num_blocks >= engine->blocks_capacity) {
        size_t new_cap = engine->blocks_capacity * 2;
        hbi_stream_block **new_blocks =
            (hbi_stream_block **)realloc(engine->blocks, new_cap * sizeof(hbi_stream_block *));
        if (!new_blocks) {
            hbi_mutex_unlock(engine->global_mutex);
            hbi_cond_destroy(block->cond);
            hbi_mutex_destroy(block->mutex);
            free(block);
            return HBI_ERR_SET(HBI_ERR_OOM, 0, "failed to reallocate blocks array");
        }
        engine->blocks = new_blocks;
        engine->blocks_capacity = new_cap;
    }

    engine->blocks[engine->num_blocks++] = block;

    hbi_mutex_unlock(engine->global_mutex);

    if (out_block) {
        *out_block = block;
    }

    return HBI_OK;
}

hbi_stream_block *hbi_stream_get_block(hbi_stream_engine *engine, uint32_t tensor_id) {
    if (!engine) {
        return NULL;
    }

    hbi_stream_block *found = NULL;
    hbi_mutex_lock(engine->global_mutex);
    for (size_t i = 0; i < engine->num_blocks; ++i) {
        if (engine->blocks[i]->tensor_id == tensor_id) {
            found = engine->blocks[i];
            break;
        }
    }
    hbi_mutex_unlock(engine->global_mutex);

    return found;
}

hbi_stream_stats_t hbi_stream_get_stats(hbi_stream_engine *engine) {
    hbi_stream_stats_t stats = {0};

    if (engine) {
        hbi_mutex_lock(engine->global_mutex);
        stats = engine->stats;
        hbi_mutex_unlock(engine->global_mutex);
    }
    return stats;
}

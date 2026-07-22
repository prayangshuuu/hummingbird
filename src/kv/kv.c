/* kv.c — KV Cache Manager & Context Runtime implementation (RFC-012). */
#include "kv/kv_internal.h"
#include <string.h>

/* ── Default Contiguous Allocator ────────────────────────────────────────── */

hbi_status hbi_kv_allocator_contiguous_allocate(void *ctx, uint32_t capacity, hbi_dtype dtype,
                                                const hbi_shape *k_shape, const hbi_shape *v_shape,
                                                hbi_kv_page **out_pages, uint32_t *out_num_pages) {
    hbi_kv_manager *manager = (hbi_kv_manager *)ctx;
    if (!manager || !k_shape || !v_shape || !out_pages || !out_num_pages) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "contiguous_allocator: NULL arguments");
    }

    hbi_kv_page *page =
        (hbi_kv_page *)hbi_alloc(manager->base_allocator, sizeof(hbi_kv_page), 8, HBI_MEM_KV);
    if (!page) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "contiguous_allocator: page alloc failed");
    }

    hbi_status st;
    st = hbi_tensor_alloc_aligned(&page->k_tensor, dtype, k_shape, HBI_TENSOR_DEFAULT_ALIGN);
    if (st != HBI_OK) {
        hbi_free(manager->base_allocator, page);
        return st;
    }

    st = hbi_tensor_alloc_aligned(&page->v_tensor, dtype, v_shape, HBI_TENSOR_DEFAULT_ALIGN);
    if (st != HBI_OK) {
        hbi_tensor_destroy(&page->k_tensor);
        hbi_free(manager->base_allocator, page);
        return st;
    }

    page->capacity = capacity;
    page->num_tokens = 0;

    *out_pages = page;
    *out_num_pages = 1;
    return HBI_OK;
}

void hbi_kv_allocator_contiguous_free(void *ctx, hbi_kv_page *pages, uint32_t num_pages) {
    hbi_kv_manager *manager = (hbi_kv_manager *)ctx;
    if (!manager || !pages)
        return;

    for (uint32_t i = 0; i < num_pages; ++i) {
        hbi_tensor_destroy(&pages[i].k_tensor);
        hbi_tensor_destroy(&pages[i].v_tensor);
    }
    hbi_free(manager->base_allocator, pages);
}

const hbi_kv_allocator g_contiguous_allocator = {.name = "contiguous",
                                                 .allocate = hbi_kv_allocator_contiguous_allocate,
                                                 .free = hbi_kv_allocator_contiguous_free};

/* ── Manager Lifecycle ───────────────────────────────────────────────────── */

hbi_status hbi_kv_manager_create(hbi_allocator *base_allocator,
                                 const hbi_kv_allocator *kv_alloc_override,
                                 hbi_kv_manager **out_manager) {
    if (!base_allocator || !out_manager) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "manager create: NULL args");
    }

    hbi_kv_manager *mgr =
        (hbi_kv_manager *)hbi_alloc(base_allocator, sizeof(hbi_kv_manager), 8, HBI_MEM_GENERAL);
    if (!mgr) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "manager create: alloc failed");
    }
    memset(mgr, 0, sizeof(*mgr));

    mgr->base_allocator = base_allocator;
    mgr->allocator = kv_alloc_override ? kv_alloc_override : &g_contiguous_allocator;

    *out_manager = mgr;
    return HBI_OK;
}

void hbi_kv_manager_destroy(hbi_kv_manager *manager) {
    if (!manager)
        return;

    /* Destroy any lingering contexts */
    for (uint32_t i = 0; i < manager->num_contexts; ++i) {
        if (manager->contexts[i]) {
            hbi_kv_context_destroy(manager, manager->contexts[i]);
        }
    }

    hbi_free(manager->base_allocator, manager);
}

hbi_status hbi_kv_manager_get_statistics(const hbi_kv_manager *manager,
                                         hbi_kv_statistics *out_stats) {
    if (!manager || !out_stats) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "manager stats: NULL args");
    }
    *out_stats = manager->stats;
    return HBI_OK;
}

/* ── Context Lifecycle ───────────────────────────────────────────────────── */

hbi_status hbi_kv_context_create(hbi_kv_manager *manager, uint32_t max_tokens, hbi_dtype dtype,
                                 const hbi_shape *k_shape, const hbi_shape *v_shape,
                                 hbi_context_handle **out_handle) {
    if (!manager || !k_shape || !v_shape || !out_handle) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "context create: NULL args");
    }
    if (manager->num_contexts >= HBI_KV_MAX_CONTEXTS) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "context create: max contexts reached");
    }

    hbi_context_handle *ctx = (hbi_context_handle *)hbi_alloc(
        manager->base_allocator, sizeof(hbi_context_handle), 8, HBI_MEM_GENERAL);
    if (!ctx) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "context create: alloc failed");
    }
    memset(ctx, 0, sizeof(*ctx));

    hbi_status st = manager->allocator->allocate(manager, max_tokens, dtype, k_shape, v_shape,
                                                 &ctx->pages, &ctx->state.num_pages);
    if (st != HBI_OK) {
        hbi_free(manager->base_allocator, ctx);
        return st;
    }

    ctx->state.max_tokens = max_tokens;
    ctx->state.total_tokens = 0;
    ctx->dtype = dtype;
    ctx->k_shape = *k_shape;
    ctx->v_shape = *v_shape;

    /* Add to manager */
    manager->contexts[manager->num_contexts++] = ctx;

    /* Update stats */
    manager->stats.active_contexts++;
    if (manager->stats.active_contexts > manager->stats.peak_contexts) {
        manager->stats.peak_contexts = manager->stats.active_contexts;
    }
    manager->stats.total_allocations++;

    /* Calculate rough memory byte size */
    size_t k_bytes = hbi_tensor_nbytes(&ctx->pages[0].k_tensor);
    size_t v_bytes = hbi_tensor_nbytes(&ctx->pages[0].v_tensor);
    manager->stats.current_memory_bytes += (k_bytes + v_bytes) * ctx->state.num_pages;
    if (manager->stats.current_memory_bytes > manager->stats.peak_memory_bytes) {
        manager->stats.peak_memory_bytes = manager->stats.current_memory_bytes;
    }

    *out_handle = ctx;
    return HBI_OK;
}

void hbi_kv_context_destroy(hbi_kv_manager *manager, hbi_context_handle *handle) {
    if (!manager || !handle)
        return;

    /* Subtract memory stats */
    size_t k_bytes = hbi_tensor_nbytes(&handle->pages[0].k_tensor);
    size_t v_bytes = hbi_tensor_nbytes(&handle->pages[0].v_tensor);
    manager->stats.current_memory_bytes -= (k_bytes + v_bytes) * handle->state.num_pages;
    manager->stats.active_contexts--;
    manager->stats.total_frees++;

    /* Remove from manager array */
    for (uint32_t i = 0; i < manager->num_contexts; ++i) {
        if (manager->contexts[i] == handle) {
            manager->contexts[i] = manager->contexts[manager->num_contexts - 1];
            manager->num_contexts--;
            break;
        }
    }

    manager->allocator->free(manager, handle->pages, handle->state.num_pages);
    hbi_free(manager->base_allocator, handle);
}

hbi_status hbi_kv_context_reset(hbi_kv_manager *manager, hbi_context_handle *handle) {
    HB_UNUSED(manager);
    if (!handle) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "context reset: NULL handle");
    }
    handle->state.total_tokens = 0;
    for (uint32_t i = 0; i < handle->state.num_pages; ++i) {
        handle->pages[i].num_tokens = 0;
    }
    return HBI_OK;
}

hbi_status hbi_kv_context_clone(hbi_kv_manager *manager, const hbi_context_handle *src,
                                hbi_context_handle **out_handle) {
    if (!manager || !src || !out_handle) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "context clone: NULL args");
    }

    hbi_status st = hbi_kv_context_create(manager, src->state.max_tokens, src->dtype, &src->k_shape,
                                          &src->v_shape, out_handle);
    if (st != HBI_OK)
        return st;

    hbi_context_handle *dst = *out_handle;
    dst->state.total_tokens = src->state.total_tokens;

    for (uint32_t i = 0; i < src->state.num_pages; ++i) {
        dst->pages[i].num_tokens = src->pages[i].num_tokens;
        /* Copy tensor memory */
        hbi_tensor_copy_into(&src->pages[i].k_tensor, &dst->pages[i].k_tensor);
        hbi_tensor_copy_into(&src->pages[i].v_tensor, &dst->pages[i].v_tensor);
    }

    return HBI_OK;
}

hbi_status hbi_kv_context_resize(hbi_kv_manager *manager, hbi_context_handle *handle,
                                 uint32_t new_max_tokens) {
    HB_UNUSED(manager);
    HB_UNUSED(handle);
    HB_UNUSED(new_max_tokens);
    return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "context resize: not yet implemented");
}

hbi_status hbi_kv_context_get_state(const hbi_kv_manager *manager, const hbi_context_handle *handle,
                                    hbi_context_state *out_state) {
    HB_UNUSED(manager);
    if (!handle || !out_state) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "context get_state: NULL args");
    }
    *out_state = handle->state;
    return HBI_OK;
}

/* ── Data Management ─────────────────────────────────────────────────────── */

hbi_status hbi_kv_context_append_tokens(hbi_kv_manager *manager, hbi_context_handle *handle,
                                        uint32_t num_tokens) {
    HB_UNUSED(manager);
    if (!handle) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "context append: NULL handle");
    }
    if (handle->state.total_tokens + num_tokens > handle->state.max_tokens) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0,
                            "context append: exceeds max_tokens (%u + %u > %u)",
                            handle->state.total_tokens, num_tokens, handle->state.max_tokens);
    }

    /* Assign tokens to pages (initially assumes contiguous layout with 1 big page) */
    uint32_t tokens_to_distribute = num_tokens;
    for (uint32_t i = 0; i < handle->state.num_pages && tokens_to_distribute > 0; ++i) {
        uint32_t page_avail = handle->pages[i].capacity - handle->pages[i].num_tokens;
        uint32_t take = HB_MIN(page_avail, tokens_to_distribute);
        handle->pages[i].num_tokens += take;
        tokens_to_distribute -= take;
    }
    handle->state.total_tokens += num_tokens;

    return HBI_OK;
}

hbi_status hbi_kv_context_truncate(hbi_kv_manager *manager, hbi_context_handle *handle,
                                   uint32_t retain_tokens) {
    HB_UNUSED(manager);
    if (!handle) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "context truncate: NULL handle");
    }
    if (retain_tokens >= handle->state.total_tokens) {
        return HBI_OK;
    }

    /* Truncate from the end */
    uint32_t total = 0;
    for (uint32_t i = 0; i < handle->state.num_pages; ++i) {
        if (total + handle->pages[i].num_tokens > retain_tokens) {
            handle->pages[i].num_tokens = retain_tokens - total;
        }
        total += handle->pages[i].num_tokens;
    }
    handle->state.total_tokens = retain_tokens;

    return HBI_OK;
}

hbi_status hbi_kv_context_get_page(const hbi_kv_manager *manager, const hbi_context_handle *handle,
                                   uint32_t page_index, const hbi_kv_page **out_page) {
    HB_UNUSED(manager);
    if (!handle || !out_page) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "context get_page: NULL args");
    }
    if (page_index >= handle->state.num_pages) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "context get_page: index %u out of bounds",
                            page_index);
    }
    *out_page = &handle->pages[page_index];
    return HBI_OK;
}

/* ── Module Identity ─────────────────────────────────────────────────────── */

const char *hbi_kv_name(void) {
    return "kv";
}

hbi_status hbi_kv_selftest(void) {
    return HBI_OK;
}

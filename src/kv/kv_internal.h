/* kv_internal.h — private to the `kv` module.
 *
 * Concrete definitions for opaque handles declared in kv.h.
 * Nothing here is visible to other modules.
 */
#ifndef HB_KV_INTERNAL_H
#define HB_KV_INTERNAL_H

#include "kv/kv.h"

/* ── Context Handle ──────────────────────────────────────────────────────── */

struct hbi_context_handle {
    hbi_context_state state;
    hbi_kv_page *pages;
    hbi_dtype dtype;
    hbi_shape k_shape;
    hbi_shape v_shape;
};

/* ── Manager ─────────────────────────────────────────────────────────────── */

#define HBI_KV_MAX_CONTEXTS 1024u

struct hbi_kv_manager {
    hbi_allocator *base_allocator;
    const hbi_kv_allocator *allocator;
    hbi_kv_statistics stats;

    /* Active context handles */
    hbi_context_handle *contexts[HBI_KV_MAX_CONTEXTS];
    uint32_t num_contexts;
};

/* ── Built-in Contiguous Allocator ───────────────────────────────────────── */

/*
 * The contiguous allocator implementation allocates a single page
 * per context containing the K and V tensors scaled to capacity.
 */
hbi_status hbi_kv_allocator_contiguous_allocate(void *ctx, uint32_t capacity, hbi_dtype dtype,
                                                const hbi_shape *k_shape, const hbi_shape *v_shape,
                                                hbi_kv_page **out_pages, uint32_t *out_num_pages);

void hbi_kv_allocator_contiguous_free(void *ctx, hbi_kv_page *pages, uint32_t num_pages);

extern const hbi_kv_allocator g_contiguous_allocator;

#endif /* HB_KV_INTERNAL_H */

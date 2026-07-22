/* kv.h — KV Cache Manager & Context Runtime (RFC-012).
 *
 * Core-public header for the `kv` module (layer 8). Other modules include this;
 * external embedders use <hummingbird/hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee).
 *
 * ── Design ───────────────────────────────────────────────────────────────────
 * The KV Cache Manager handles the allocation and lifecycle of attention state
 * across multiple generation contexts. It is backend- and model-independent.
 * It manages token cursors and capacity, separating memory management from
 * actual attention kernels.
 *
 * ── Abstractions ─────────────────────────────────────────────────────────────
 * - hbi_kv_manager: The global subsystem holding memory limits and allocators.
 * - hbi_context_handle: An opaque reference to a single generation sequence.
 * - hbi_kv_page: A contiguous block of tokens for K and V.
 * - hbi_kv_allocator: A vtable abstracting where KV memory comes from
 *                     (allows migrating from contiguous -> paged -> NUMA -> GPU).
 */
#ifndef HB_KV_H
#define HB_KV_H

#include "common/common.h"
#include "memory/memory.h"
#include "tensor/tensor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Types & Structures ──────────────────────────────────────────────────── */

typedef struct hbi_kv_manager hbi_kv_manager;
typedef struct hbi_context_handle hbi_context_handle;

/* Represents one contiguous block of KV memory for a context.
 * In the initial contiguous allocator, there is exactly 1 page per context.
 * In a future paged allocator, a context will have N pages.
 * The shapes of k_tensor and v_tensor are provided by the caller during
 * context creation (e.g., [batch, heads, seq, head_dim]). */
typedef struct hbi_kv_page {
    hbi_tensor k_tensor; /* Borrowed or View tensor representing Keys */
    hbi_tensor v_tensor; /* Borrowed or View tensor representing Values */
    uint32_t capacity;   /* Max tokens this page can hold */
    uint32_t num_tokens; /* Tokens currently stored in this page */
} hbi_kv_page;

/* Snapshot of a context's current state. */
typedef struct hbi_context_state {
    uint32_t total_tokens; /* Tokens currently appended */
    uint32_t max_tokens;   /* Total capacity reserved */
    uint32_t num_pages;    /* Number of allocated pages */
} hbi_context_state;

/* Global statistics for the KV Cache Manager. */
typedef struct hbi_kv_statistics {
    uint32_t active_contexts;
    uint32_t peak_contexts;
    size_t current_memory_bytes;
    size_t peak_memory_bytes;
    uint64_t total_allocations;
    uint64_t total_frees;
} hbi_kv_statistics;

/* ── KV Allocator VTable ───────────────────────────────────────────────────
 * Abstract interface for allocating K and V tensors.
 * Implementations manage the placement (RAM vs VRAM) and topology (Paged vs Contig). */
typedef struct hbi_kv_allocator {
    const char *name;

    /* Allocate one or more pages for a context.
     * `k_shape` and `v_shape` describe the required dimensions for `capacity` tokens.
     * The allocator yields `out_pages` and sets `out_num_pages`. */
    hbi_status (*allocate)(void *ctx, uint32_t capacity, hbi_dtype dtype, const hbi_shape *k_shape,
                           const hbi_shape *v_shape, hbi_kv_page **out_pages,
                           uint32_t *out_num_pages);

    /* Free pages previously returned by `allocate`. */
    void (*free)(void *ctx, hbi_kv_page *pages, uint32_t num_pages);
} hbi_kv_allocator;

/* ── Manager Lifecycle ───────────────────────────────────────────────────── */

/* Create a KV Manager. If `kv_alloc_override` is NULL, uses the default
 * contiguous allocator backed by `base_allocator`. */
hbi_status hbi_kv_manager_create(hbi_allocator *base_allocator,
                                 const hbi_kv_allocator *kv_alloc_override,
                                 hbi_kv_manager **out_manager);

/* Destroy the manager and forcefully free all active contexts. */
void hbi_kv_manager_destroy(hbi_kv_manager *manager);

/* Retrieve global statistics. */
hbi_status hbi_kv_manager_get_statistics(const hbi_kv_manager *manager,
                                         hbi_kv_statistics *out_stats);

/* ── Context Lifecycle ───────────────────────────────────────────────────── */

/* Create a new empty context capable of holding `max_tokens`.
 * `k_shape` and `v_shape` define the tensor dimensions at maximum capacity. */
hbi_status hbi_kv_context_create(hbi_kv_manager *manager, uint32_t max_tokens, hbi_dtype dtype,
                                 const hbi_shape *k_shape, const hbi_shape *v_shape,
                                 hbi_context_handle **out_handle);

/* Destroy a context and free its memory back to the allocator. */
void hbi_kv_context_destroy(hbi_kv_manager *manager, hbi_context_handle *handle);

/* Reset a context to 0 tokens without freeing its underlying memory. */
hbi_status hbi_kv_context_reset(hbi_kv_manager *manager, hbi_context_handle *handle);

/* Deep clone `src` into `out_handle`. The clone has independent memory.
 * Fails with HBI_ERR_OOM if the allocator rejects the new capacity. */
hbi_status hbi_kv_context_clone(hbi_kv_manager *manager, const hbi_context_handle *src,
                                hbi_context_handle **out_handle);

/* Resize a context's capacity. If smaller, tokens beyond `new_max_tokens` are dropped.
 * If larger, attempts reallocation/expansion. */
hbi_status hbi_kv_context_resize(hbi_kv_manager *manager, hbi_context_handle *handle,
                                 uint32_t new_max_tokens);

/* Retrieve the current state of a context. */
hbi_status hbi_kv_context_get_state(const hbi_kv_manager *manager, const hbi_context_handle *handle,
                                    hbi_context_state *out_state);

/* ── Data Management ─────────────────────────────────────────────────────── */

/* Append `num_tokens` to the context cursor.
 * Fails with HBI_ERR_INVALID_ARG if it exceeds `max_tokens`.
 * This does NOT write data—it advances the metadata cursors so the
 * executor knows where to write the new KV embeddings. */
hbi_status hbi_kv_context_append_tokens(hbi_kv_manager *manager, hbi_context_handle *handle,
                                        uint32_t num_tokens);

/* Truncate the context to retain only the first `retain_tokens`.
 * If `retain_tokens` >= current tokens, does nothing. */
hbi_status hbi_kv_context_truncate(hbi_kv_manager *manager, hbi_context_handle *handle,
                                   uint32_t retain_tokens);

/* Retrieve a specific KV page (0 to num_pages-1).
 * The returned page struct allows the executor to view/write the tensors. */
hbi_status hbi_kv_context_get_page(const hbi_kv_manager *manager, const hbi_context_handle *handle,
                                   uint32_t page_index, const hbi_kv_page **out_page);

/* ── Module Identity ─────────────────────────────────────────────────────── */

const char *hbi_kv_name(void);
hbi_status hbi_kv_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_KV_H */

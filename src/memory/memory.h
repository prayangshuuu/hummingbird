/* memory.h — Allocator interface + built-in allocators, statistics, and debug
 * checks (§3.3, DD-023). This is the FOUNDATION allocator layer: the tier model
 * (VRAM/RAM/NVMe placement), RAM-budget/OOM guard, and NUMA binding described in
 * PROJECT_CONTEXT §3.3 are built ON TOP of this interface in a later phase — they
 * are deliberately NOT here yet (this phase builds no inference/placement logic).
 *
 * Core-public header for the `memory` module (layer 4). Symbols are prefixed
 * `hbi_` (internal, no stability guarantee); external embedders use
 * <hummingbird/hummingbird.h>.
 *
 * The core abstraction is hbi_allocator: a vtable + context, so any subsystem
 * can be handed an allocator without knowing whether it is the system heap, a
 * bump arena, or (later) a NUMA-bound or GPU-host allocator. Two implementations
 * ship now:
 *   - SYSTEM: malloc/aligned-alloc backed, thread-safe, tracks statistics.
 *   - ARENA:  a linear bump allocator over one backing block; frees are no-ops
 *             (reset frees everything at once). Not thread-safe by design (one
 *             arena per worker), which is exactly what a per-request scratch
 *             buffer wants.
 *
 * Ownership: an allocator does not own memory handed to another allocator. Every
 * block from alloc/realloc must be returned to the SAME allocator's free.
 *
 * Thread-safety: the SYSTEM allocator is fully thread-safe. The ARENA is not;
 * serialize externally or give each thread its own. Statistics on the system
 * allocator are atomic.
 */
#ifndef HB_MEMORY_H
#define HB_MEMORY_H

#include "common/common.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Allocation tag (DD-009 groundwork) ──────────────────────────────────────
 * A coarse purpose label carried on each allocation for statistics and future
 * placement policy. It does NOT change where memory comes from yet — it is the
 * hook the tiered manager will dispatch on later. */
typedef enum hbi_mem_tag {
    HBI_MEM_GENERAL = 0, /* miscellaneous engine allocations */
    HBI_MEM_WEIGHTS,     /* model weight buffers (streamed or resident) */
    HBI_MEM_KV,          /* attention key/value cache */
    HBI_MEM_SCRATCH,     /* transient per-op scratch */
    HBI_MEM_TAG_COUNT    /* sentinel */
} hbi_mem_tag;

const char *hbi_mem_tag_str(hbi_mem_tag tag);

/* ── Allocator interface (DD-023) ────────────────────────────────────────────
 * A vtable of function pointers + an opaque context. `alignment` must be a power
 * of two (0 means "natural", treated as alignof(max_align_t)). alloc/realloc
 * return NULL on failure and set the common error record. free(NULL) is a no-op.
 * realloc(NULL, n) behaves like alloc; realloc(p, 0) frees and returns NULL. */
typedef struct hbi_allocator hbi_allocator;

typedef struct hbi_allocator_vtable {
    void *(*alloc)(void *ctx, size_t size, size_t alignment, hbi_mem_tag tag);
    void *(*realloc)(void *ctx, void *ptr, size_t new_size, size_t alignment, hbi_mem_tag tag);
    void (*free)(void *ctx, void *ptr);
    const char *name; /* stable identifier for diagnostics ("system"/"arena") */
} hbi_allocator_vtable;

struct hbi_allocator {
    const hbi_allocator_vtable *vt;
    void *ctx;
};

/* Thin dispatchers so call sites read naturally and NULL-check once. */
void *hbi_alloc(hbi_allocator *a, size_t size, size_t alignment, hbi_mem_tag tag);
void *hbi_realloc(hbi_allocator *a, void *ptr, size_t new_size, size_t alignment, hbi_mem_tag tag);
void hbi_free(hbi_allocator *a, void *ptr);

/* ── Statistics ──────────────────────────────────────────────────────────────
 * Exact accounting in the spirit of Colibrì's qt_bytes (§3.3): every allocator
 * can report live/peak bytes and counts. Per-tag byte tallies aid budgeting. */
typedef struct hbi_mem_stats {
    uint64_t live_bytes;        /* currently outstanding */
    uint64_t peak_bytes;        /* high-water mark */
    uint64_t total_alloc_calls; /* cumulative alloc+realloc-as-alloc */
    uint64_t total_free_calls;  /* cumulative frees */
    uint64_t live_blocks;       /* outstanding block count (leak check) */
    uint64_t bytes_by_tag[HBI_MEM_TAG_COUNT];
} hbi_mem_stats;

/* Snapshot an allocator's statistics. Returns HBI_ERR_UNSUPPORTED for allocators
 * that do not track (none of the built-ins). Safe on the system allocator from
 * any thread. */
hbi_status hbi_allocator_stats(const hbi_allocator *a, hbi_mem_stats *out);

/* ── System allocator ────────────────────────────────────────────────────────
 * Process-wide, thread-safe, statistics-tracking allocator over the platform
 * aligned-alloc shim. There is one shared instance; hbi_allocator_system()
 * returns it (never NULL, no init needed). Debug mode (below) applies to it. */
hbi_allocator *hbi_allocator_system(void);

/* ── Debug mode ──────────────────────────────────────────────────────────────
 * When enabled, the system allocator adds red-zone canaries around each block
 * and validates them on free (catching overflow/underflow), and maintains a
 * live-block table so hbi_allocator_check_leaks() can report leaks. Off by
 * default; enable at startup before threads run. Portable (no OS-specific
 * tooling) so it works on every platform and in CI. */
void hbi_mem_debug_set_enabled(bool enabled);
bool hbi_mem_debug_enabled(void);

/* Return HBI_OK if no blocks are outstanding on the system allocator, else
 * HBI_ERR_STATE (records how many/how many bytes leaked). Only meaningful in
 * debug mode; returns HBI_OK otherwise. */
hbi_status hbi_mem_check_leaks(void);

/* ── Arena allocator ─────────────────────────────────────────────────────────
 * A linear bump allocator over a single backing block obtained from a parent
 * allocator (or system if parent is NULL). alloc carves aligned slices; free is
 * a no-op; reset reclaims everything at once. Ideal for per-forward scratch.
 * Not thread-safe. */
typedef struct hbi_arena hbi_arena;

/* Create an arena with `capacity` usable bytes, backed by `parent` (or the
 * system allocator when NULL). Returns HBI_ERR_INVALID_ARG (zero capacity/NULL
 * out) or HBI_ERR_OOM. */
hbi_status hbi_arena_create(hbi_arena **out, hbi_allocator *parent, size_t capacity);
void hbi_arena_destroy(hbi_arena *arena); /* NULL is a no-op */

/* View an arena as a generic allocator (its free() is a no-op; realloc grows in
 * place only when the block is the arena's last allocation, else copies). */
hbi_allocator *hbi_arena_allocator(hbi_arena *arena);

/* Reclaim all allocations at once (pointers become invalid). */
void hbi_arena_reset(hbi_arena *arena);

/* Bytes currently carved / total capacity. */
size_t hbi_arena_used(const hbi_arena *arena);
size_t hbi_arena_capacity(const hbi_arena *arena);

/* ── Module identity / self-test ─────────────────────────────────────────── */
const char *hbi_memory_name(void);
hbi_status hbi_memory_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_MEMORY_H */

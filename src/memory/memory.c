/* memory.c — allocator interface, system + arena allocators, statistics, debug.
 *
 * See memory.h for the contract. Design notes:
 *   - The system allocator stores a small header immediately before the pointer
 *     it returns, so free()/realloc()/stats work without a global block map in
 *     the fast path. The header records size, alignment, tag, and (debug) a
 *     canary + intrusive leak-list links.
 *   - All statistics are atomics so the system allocator is lock-free for
 *     counting; the debug leak list is mutex-guarded (debug is not a hot path).
 *   - The arena is a single backing block with a bump cursor; not thread-safe.
 */
#include "memory/memory_internal.h"

#include "platform/platform.h"

#include <stdatomic.h>
#include <string.h>

/* ── Tag strings ───────────────────────────────────────────────────────────── */

const char *hbi_mem_tag_str(hbi_mem_tag tag) {
    switch (tag) {
    case HBI_MEM_GENERAL:
        return "general";
    case HBI_MEM_WEIGHTS:
        return "weights";
    case HBI_MEM_KV:
        return "kv";
    case HBI_MEM_SCRATCH:
        return "scratch";
    case HBI_MEM_TAG_COUNT:
        break;
    }
    return "unknown";
}

/* ── Generic dispatchers ─────────────────────────────────────────────────────
 * A NULL allocator is a programming error, reported (not crashed) so misuse is
 * visible in tests without a segfault. */
void *hbi_alloc(hbi_allocator *a, size_t size, size_t alignment, hbi_mem_tag tag) {
    if (a == NULL || a->vt == NULL || a->vt->alloc == NULL) {
        HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "hbi_alloc: NULL allocator");
        return NULL;
    }
    return a->vt->alloc(a->ctx, size, alignment, tag);
}

void *hbi_realloc(hbi_allocator *a, void *ptr, size_t new_size, size_t alignment, hbi_mem_tag tag) {
    if (a == NULL || a->vt == NULL || a->vt->realloc == NULL) {
        HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "hbi_realloc: NULL allocator");
        return NULL;
    }
    return a->vt->realloc(a->ctx, ptr, new_size, alignment, tag);
}

void hbi_free(hbi_allocator *a, void *ptr) {
    if (a == NULL || a->vt == NULL || a->vt->free == NULL) {
        return;
    }
    a->vt->free(a->ctx, ptr);
}

/* ── Shared helpers ──────────────────────────────────────────────────────────
 * Natural alignment when the caller passes 0. */
#define HBI_MEM_NATURAL_ALIGN (sizeof(void *) * 2) /* == alignof(max_align_t) on our targets */

static size_t normalize_alignment(size_t alignment) {
    if (alignment == 0) {
        return HBI_MEM_NATURAL_ALIGN;
    }
    return alignment;
}

/* ── System allocator ─────────────────────────────────────────────────────────
 * Each allocation is laid out as:
 *     [ padding ][ header ][ user bytes ][ trailing canary (debug) ]
 * The pointer returned to the caller is `user`; header sits immediately before
 * it. We over-allocate from the platform aligned-alloc so that `user` meets the
 * requested alignment and the header fits before it. */

/* Distinctive 32-bit red-zone markers (ASCII-only hex). */
#define HBI_CANARY_HEAD 0xA10CA71Eu
#define HBI_CANARY_TAIL 0x5AFEF00Du

typedef struct sys_header {
    void *base;                  /* original pointer from platform aligned-alloc */
    size_t size;                 /* user-visible size */
    size_t alignment;            /* requested (normalized) alignment */
    hbi_mem_tag tag;             /* allocation tag */
    uint32_t canary;             /* HBI_CANARY_HEAD in debug; 0 otherwise */
    struct sys_header *dbg_next; /* debug leak list */
    struct sys_header *dbg_prev;
} sys_header;

typedef struct sys_ctx {
    atomic_ullong live_bytes;
    atomic_ullong peak_bytes;
    atomic_ullong total_alloc_calls;
    atomic_ullong total_free_calls;
    atomic_ullong live_blocks;
    atomic_ullong bytes_by_tag[HBI_MEM_TAG_COUNT];
    /* Debug leak-list, guarded by dbg_mtx (created lazily). dbg_live_blocks and
     * dbg_live_bytes count ONLY blocks tracked while debug mode was on, so the
     * leak check is immune to non-debug or pre-debug allocations. */
    hbi_mutex *dbg_mtx;
    sys_header *dbg_head;
    atomic_ullong dbg_live_blocks;
    atomic_ullong dbg_live_bytes;
} sys_ctx;

static sys_ctx g_sys_ctx;
static atomic_bool g_debug_enabled = false;
static atomic_bool g_debug_mtx_ready = false;

/* Lazily create the debug mutex once. Called only on debug-mode paths. */
static hbi_mutex *debug_mutex(void) {
    if (!atomic_load_explicit(&g_debug_mtx_ready, memory_order_acquire)) {
        /* First-time init. Debug mode is enabled at single-threaded startup, so
         * a benign race here is avoided by the documented usage; we still guard
         * with a compare-exchange to be safe. */
        hbi_mutex *m = NULL;
        if (hbi_mutex_init(&m) != HBI_OK) {
            return NULL;
        }
        bool expected = false;
        if (atomic_compare_exchange_strong_explicit(&g_debug_mtx_ready, &expected, true,
                                                    memory_order_acq_rel, memory_order_acquire)) {
            g_sys_ctx.dbg_mtx = m;
        } else {
            hbi_mutex_destroy(m); /* someone else won */
        }
    }
    return g_sys_ctx.dbg_mtx;
}

static void stats_add(sys_ctx *c, size_t size, hbi_mem_tag tag) {
    unsigned long long live =
        atomic_fetch_add_explicit(&c->live_bytes, (unsigned long long)size, memory_order_relaxed) +
        size;
    atomic_fetch_add_explicit(&c->total_alloc_calls, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&c->live_blocks, 1, memory_order_relaxed);
    if (tag < HBI_MEM_TAG_COUNT) {
        atomic_fetch_add_explicit(&c->bytes_by_tag[tag], (unsigned long long)size,
                                  memory_order_relaxed);
    }
    /* Bump peak if we raised the high-water mark (CAS loop). */
    unsigned long long peak = atomic_load_explicit(&c->peak_bytes, memory_order_relaxed);
    while (live > peak &&
           !atomic_compare_exchange_weak_explicit(&c->peak_bytes, &peak, live, memory_order_relaxed,
                                                  memory_order_relaxed)) {
        /* peak reloaded; retry */
    }
}

static void stats_sub(sys_ctx *c, size_t size, hbi_mem_tag tag) {
    atomic_fetch_sub_explicit(&c->live_bytes, (unsigned long long)size, memory_order_relaxed);
    atomic_fetch_add_explicit(&c->total_free_calls, 1, memory_order_relaxed);
    atomic_fetch_sub_explicit(&c->live_blocks, 1, memory_order_relaxed);
    if (tag < HBI_MEM_TAG_COUNT) {
        atomic_fetch_sub_explicit(&c->bytes_by_tag[tag], (unsigned long long)size,
                                  memory_order_relaxed);
    }
}

/* Space reserved before the user pointer: header + room to align. */
static size_t sys_overhead(size_t alignment) {
    /* We place the header right before `user`. `user` must be aligned; the base
     * from the platform allocator is already aligned to `alignment` (>= header
     * alignment), so we reserve one `alignment` stride to hold the header and
     * keep `user` aligned. */
    size_t head = sizeof(sys_header);
    size_t stride = alignment > head ? alignment : head;
    /* round stride up to a multiple of alignment so user stays aligned */
    return hbi_align_up(stride, alignment);
}

static uint32_t *tail_canary_ptr(sys_header *h) {
    unsigned char *user = (unsigned char *)(h + 1);
    return (uint32_t *)(void *)(user + h->size);
}

static void debug_link(sys_ctx *c, sys_header *h) {
    hbi_mutex *m = debug_mutex();
    if (m == NULL) {
        return;
    }
    hbi_mutex_lock(m);
    h->dbg_prev = NULL;
    h->dbg_next = c->dbg_head;
    if (c->dbg_head) {
        c->dbg_head->dbg_prev = h;
    }
    c->dbg_head = h;
    hbi_mutex_unlock(m);
    atomic_fetch_add_explicit(&c->dbg_live_blocks, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&c->dbg_live_bytes, (unsigned long long)h->size,
                              memory_order_relaxed);
}

static void debug_unlink(sys_ctx *c, sys_header *h) {
    hbi_mutex *m = debug_mutex();
    if (m == NULL) {
        return;
    }
    hbi_mutex_lock(m);
    if (h->dbg_prev) {
        h->dbg_prev->dbg_next = h->dbg_next;
    } else if (c->dbg_head == h) {
        c->dbg_head = h->dbg_next;
    }
    if (h->dbg_next) {
        h->dbg_next->dbg_prev = h->dbg_prev;
    }
    h->dbg_next = h->dbg_prev = NULL;
    hbi_mutex_unlock(m);
    atomic_fetch_sub_explicit(&c->dbg_live_blocks, 1, memory_order_relaxed);
    atomic_fetch_sub_explicit(&c->dbg_live_bytes, (unsigned long long)h->size,
                              memory_order_relaxed);
}

static void *sys_alloc(void *ctx, size_t size, size_t alignment, hbi_mem_tag tag) {
    sys_ctx *c = (sys_ctx *)ctx;
    bool debug = atomic_load_explicit(&g_debug_enabled, memory_order_relaxed);

    if (tag >= HBI_MEM_TAG_COUNT) {
        HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "sys_alloc: bad tag");
        return NULL;
    }
    alignment = normalize_alignment(alignment);
    if (!hbi_is_pow2(alignment)) {
        HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "sys_alloc: alignment %zu not a power of two",
                     alignment);
        return NULL;
    }
    if (size == 0) {
        size = 1; /* always return a unique freeable pointer */
    }

    size_t overhead = sys_overhead(alignment);
    size_t canary_bytes = debug ? sizeof(uint32_t) : 0;
    size_t total = overhead + size + canary_bytes;
    if (total < size) { /* overflow */
        HBI_ERR_SET(HBI_ERR_OOM, 0, "sys_alloc: size overflow");
        return NULL;
    }

    void *base = hbi_aligned_alloc(alignment, hbi_align_up(total, alignment));
    if (base == NULL) {
        HBI_ERR_SETF(HBI_ERR_OOM, hbi_os_errno(), "sys_alloc: %zu bytes (align %zu) failed", size,
                     alignment);
        return NULL;
    }

    unsigned char *user = (unsigned char *)base + overhead;
    sys_header *h = (sys_header *)(void *)(user - sizeof(sys_header));
    h->base = base;
    h->size = size;
    h->alignment = alignment;
    h->tag = tag;
    h->canary = debug ? HBI_CANARY_HEAD : 0;
    h->dbg_next = h->dbg_prev = NULL;

    if (debug) {
        *tail_canary_ptr(h) = HBI_CANARY_TAIL;
        debug_link(c, h);
    }

    stats_add(c, size, tag);
    return user;
}

/* Recover the header from a user pointer. */
static sys_header *sys_header_of(void *user) {
    return (sys_header *)(void *)((unsigned char *)user - sizeof(sys_header));
}

static void sys_free(void *ctx, void *ptr) {
    sys_ctx *c = (sys_ctx *)ctx;
    if (ptr == NULL) {
        return;
    }
    sys_header *h = sys_header_of(ptr);
    bool debug = atomic_load_explicit(&g_debug_enabled, memory_order_relaxed);

    if (debug && h->canary == HBI_CANARY_HEAD) {
        /* Validate red zones; a mismatch means overflow/underflow occurred. */
        if (*tail_canary_ptr(h) != HBI_CANARY_TAIL) {
            HBI_ERR_SET(HBI_ERR_CORRUPT, 0, "sys_free: trailing canary corrupt (buffer overflow)");
        }
        debug_unlink(c, h);
    }

    stats_sub(c, h->size, h->tag);
    hbi_aligned_free(h->base);
}

static void *sys_realloc(void *ctx, void *ptr, size_t new_size, size_t alignment, hbi_mem_tag tag) {
    if (ptr == NULL) {
        return sys_alloc(ctx, new_size, alignment, tag);
    }
    if (new_size == 0) {
        sys_free(ctx, ptr);
        return NULL;
    }
    /* Simple, correct realloc: allocate new, copy min, free old. Keeps the
     * header/canary invariants without special-casing in-place growth. */
    sys_header *h = sys_header_of(ptr);
    void *fresh = sys_alloc(ctx, new_size, alignment ? alignment : h->alignment, tag);
    if (fresh == NULL) {
        return NULL; /* error already set; old block untouched */
    }
    size_t copy = h->size < new_size ? h->size : new_size;
    memcpy(fresh, ptr, copy);
    sys_free(ctx, ptr);
    return fresh;
}

static const hbi_allocator_vtable g_sys_vtable = {
    .alloc = sys_alloc,
    .realloc = sys_realloc,
    .free = sys_free,
    .name = "system",
};

static hbi_allocator g_sys_allocator = {
    .vt = &g_sys_vtable,
    .ctx = &g_sys_ctx,
};

hbi_allocator *hbi_allocator_system(void) {
    return &g_sys_allocator;
}

/* ── Statistics readback ─────────────────────────────────────────────────────── */

hbi_status hbi_allocator_stats(const hbi_allocator *a, hbi_mem_stats *out) {
    if (a == NULL || out == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
    if (a->vt != &g_sys_vtable) {
        /* Only the system allocator tracks statistics in this phase. */
        return HBI_ERR_UNSUPPORTED;
    }
    sys_ctx *c = (sys_ctx *)a->ctx;
    memset(out, 0, sizeof(*out));
    out->live_bytes = atomic_load_explicit(&c->live_bytes, memory_order_relaxed);
    out->peak_bytes = atomic_load_explicit(&c->peak_bytes, memory_order_relaxed);
    out->total_alloc_calls = atomic_load_explicit(&c->total_alloc_calls, memory_order_relaxed);
    out->total_free_calls = atomic_load_explicit(&c->total_free_calls, memory_order_relaxed);
    out->live_blocks = atomic_load_explicit(&c->live_blocks, memory_order_relaxed);
    for (int i = 0; i < HBI_MEM_TAG_COUNT; ++i) {
        out->bytes_by_tag[i] = atomic_load_explicit(&c->bytes_by_tag[i], memory_order_relaxed);
    }
    return HBI_OK;
}

/* ── Debug mode ────────────────────────────────────────────────────────────── */

void hbi_mem_debug_set_enabled(bool enabled) {
    if (enabled) {
        (void)debug_mutex(); /* ensure the mutex exists before first debug alloc */
    }
    atomic_store_explicit(&g_debug_enabled, enabled, memory_order_relaxed);
}

bool hbi_mem_debug_enabled(void) {
    return atomic_load_explicit(&g_debug_enabled, memory_order_relaxed);
}

hbi_status hbi_mem_check_leaks(void) {
    unsigned long long live =
        atomic_load_explicit(&g_sys_ctx.dbg_live_blocks, memory_order_relaxed);
    if (live == 0) {
        return HBI_OK;
    }
    return HBI_ERR_SETF(HBI_ERR_STATE, 0, "memory leak: %llu debug-tracked block(s) outstanding",
                        live);
}

/* ── Arena allocator ──────────────────────────────────────────────────────────
 * One backing block + bump cursor. free() is a no-op; realloc grows in place
 * only for the most recent allocation, otherwise copies within the arena. */

struct hbi_arena {
    hbi_allocator *parent;  /* who owns the backing block */
    unsigned char *base;    /* backing block */
    size_t capacity;        /* usable bytes */
    size_t offset;          /* bump cursor */
    size_t last_offset;     /* start of the most recent allocation */
    hbi_allocator as_alloc; /* view of this arena as a generic allocator */
};

static void *arena_alloc(void *ctx, size_t size, size_t alignment, hbi_mem_tag tag) {
    HB_UNUSED(tag);
    hbi_arena *ar = (hbi_arena *)ctx;
    alignment = normalize_alignment(alignment);
    if (!hbi_is_pow2(alignment)) {
        HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "arena_alloc: bad alignment");
        return NULL;
    }
    if (size == 0) {
        size = 1;
    }
    /* Align the ABSOLUTE address, not just the offset: the backing block's base
     * carries only natural alignment, so aligning the offset alone would not make
     * base+offset meet a larger requested alignment. Compute the aligned address,
     * then derive the offset from it. */
    uintptr_t base_addr = (uintptr_t)ar->base;
    uintptr_t cur_addr = base_addr + ar->offset;
    uintptr_t aligned_addr = (uintptr_t)hbi_align_up((size_t)cur_addr, alignment);
    if (aligned_addr == 0 && cur_addr != 0) { /* alignment overflow */
        HBI_ERR_SET(HBI_ERR_OOM, 0, "arena_alloc: alignment overflow");
        return NULL;
    }
    size_t start = (size_t)(aligned_addr - base_addr);
    size_t end = start + size;
    if (end < start || end > ar->capacity) {
        HBI_ERR_SETF(HBI_ERR_OOM, 0, "arena_alloc: %zu bytes exhausts arena (used %zu/%zu)", size,
                     ar->offset, ar->capacity);
        return NULL;
    }
    ar->last_offset = start;
    ar->offset = end;
    return ar->base + start;
}

static void *arena_realloc(void *ctx, void *ptr, size_t new_size, size_t alignment,
                           hbi_mem_tag tag) {
    hbi_arena *ar = (hbi_arena *)ctx;
    if (ptr == NULL) {
        return arena_alloc(ctx, new_size, alignment, tag);
    }
    if (new_size == 0) {
        return NULL; /* free is a no-op in an arena */
    }
    /* Grow in place if ptr is the last allocation. */
    if ((unsigned char *)ptr == ar->base + ar->last_offset) {
        size_t end = ar->last_offset + new_size;
        if (end >= ar->last_offset && end <= ar->capacity) {
            ar->offset = end;
            return ptr;
        }
    }
    /* Otherwise carve a fresh slice and copy. */
    void *fresh = arena_alloc(ctx, new_size, alignment, tag);
    if (fresh == NULL) {
        return NULL;
    }
    /* We do not know the old size here; copy up to new_size, bounded by what
     * remains from ptr to the cursor at call time. Arena realloc of a non-last
     * block is discouraged; callers needing this should track sizes. */
    size_t old_avail = (size_t)((ar->base + ar->offset) - (unsigned char *)ptr);
    size_t copy = new_size < old_avail ? new_size : old_avail;
    memcpy(fresh, ptr, copy);
    return fresh;
}

static void arena_free(void *ctx, void *ptr) {
    HB_UNUSED(ctx);
    HB_UNUSED(ptr);
    /* No-op: an arena frees everything at reset/destroy. */
}

static const hbi_allocator_vtable g_arena_vtable = {
    .alloc = arena_alloc,
    .realloc = arena_realloc,
    .free = arena_free,
    .name = "arena",
};

hbi_status hbi_arena_create(hbi_arena **out, hbi_allocator *parent, size_t capacity) {
    if (out == NULL || capacity == 0) {
        return HBI_ERR_INVALID_ARG;
    }
    *out = NULL;
    hbi_allocator *p = parent ? parent : hbi_allocator_system();

    hbi_arena *ar = (hbi_arena *)hbi_alloc(p, sizeof(*ar), 0, HBI_MEM_GENERAL);
    if (ar == NULL) {
        return HBI_ERR_OOM;
    }
    ar->base = (unsigned char *)hbi_alloc(p, capacity, HBI_MEM_NATURAL_ALIGN, HBI_MEM_SCRATCH);
    if (ar->base == NULL) {
        hbi_free(p, ar);
        return HBI_ERR_OOM;
    }
    ar->parent = p;
    ar->capacity = capacity;
    ar->offset = 0;
    ar->last_offset = 0;
    ar->as_alloc.vt = &g_arena_vtable;
    ar->as_alloc.ctx = ar;
    *out = ar;
    return HBI_OK;
}

void hbi_arena_destroy(hbi_arena *arena) {
    if (arena == NULL) {
        return;
    }
    hbi_allocator *p = arena->parent;
    hbi_free(p, arena->base);
    hbi_free(p, arena);
}

hbi_allocator *hbi_arena_allocator(hbi_arena *arena) {
    return arena ? &arena->as_alloc : NULL;
}

void hbi_arena_reset(hbi_arena *arena) {
    if (arena) {
        arena->offset = 0;
        arena->last_offset = 0;
    }
}

size_t hbi_arena_used(const hbi_arena *arena) {
    return arena ? arena->offset : 0;
}

size_t hbi_arena_capacity(const hbi_arena *arena) {
    return arena ? arena->capacity : 0;
}

/* ── Module identity / self-test ─────────────────────────────────────────────── */

const char *hbi_memory_name(void) {
    return "memory";
}

hbi_status hbi_memory_selftest(void) {
    /* Round-trip a small allocation through the system allocator and confirm the
     * statistics move as expected. Runs regardless of debug mode. */
    hbi_allocator *a = hbi_allocator_system();
    hbi_mem_stats before;
    if (hbi_allocator_stats(a, &before) != HBI_OK) {
        return HBI_ERR_INTERNAL;
    }
    void *p = hbi_alloc(a, 128, 64, HBI_MEM_GENERAL);
    if (p == NULL || ((uintptr_t)p % 64u) != 0) {
        return HBI_ERR_INTERNAL;
    }
    hbi_mem_stats mid;
    hbi_allocator_stats(a, &mid);
    if (mid.live_bytes < before.live_bytes + 128) {
        hbi_free(a, p);
        return HBI_ERR_INTERNAL;
    }
    hbi_free(a, p);
    hbi_mem_stats after;
    hbi_allocator_stats(a, &after);
    if (after.live_bytes != before.live_bytes) {
        return HBI_ERR_INTERNAL;
    }
    return HBI_OK;
}

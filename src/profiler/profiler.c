/* profiler.c — lightweight timing/counter/event instrumentation.
 *
 * Implementation notes:
 *   - A fixed table of HBI_PROF_MAX_NAMES slots. A slot is claimed by the first
 *     recorder of a name; lookup is a linear scan with a cached FNV-1a hash to
 *     skip mismatches cheaply (the table is small and mostly warm). Claiming is
 *     guarded by one mutex; recording into an already-claimed slot is lock-free
 *     (atomic fetch-add on the aggregate fields).
 *   - "Disabled" is a single relaxed atomic load on every entry point, so a
 *     release build with profiling off pays ~one predictable branch per call and
 *     never reads the clock.
 *   - Nothing allocates after slot claim; names are stored by pointer (identity)
 *     and the report reads their content.
 */
#include "profiler/profiler_internal.h"

#include "platform/platform.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* One instrumentation name and its accumulated statistics. All mutable fields
 * are atomic so recording needs no lock once the slot is claimed. */
typedef struct prof_slot {
    _Atomic(const char *) name; /* NULL => unclaimed; set once under g_mx */
    uint32_t hash;              /* FNV-1a of the name, for fast skip */
    _Atomic int kind;           /* hbi_prof_kind, set at claim */
    _Atomic uint64_t count;     /* calls / updates / marks */
    _Atomic int64_t total;      /* counter/event running sum */
    _Atomic uint64_t total_ns;  /* scope: summed durations */
    _Atomic uint64_t min_ns;    /* scope: fastest (UINT64_MAX sentinel) */
    _Atomic uint64_t max_ns;    /* scope: slowest */
} prof_slot;

static prof_slot g_slots[HBI_PROF_MAX_NAMES];
static atomic_size_t g_slot_count;      /* number of claimed slots */
static atomic_bool g_enabled;           /* master switch (default off) */
static atomic_uint_fast64_t g_overflow; /* names dropped: table full */
static hbi_mutex *g_claim_mx;           /* serializes slot claiming */
static atomic_bool g_mx_ready;          /* g_claim_mx initialized */

/* FNV-1a over a NUL-terminated string. */
static uint32_t fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; ++s) {
        h ^= (uint32_t)(unsigned char)*s;
        h *= 16777619u;
    }
    return h;
}

/* Lazily create the claim mutex on first use. The double-checked flag keeps the
 * common (already-initialized) path lock-free. Racing first-time creators are
 * serialized by the fact that mutex_init is cheap and idempotent losers free
 * their extra mutex. To stay simple and correct we create under a tiny spin on
 * the flag using a compare-exchange to elect one creator. */
static void ensure_mutex(void) {
    if (atomic_load_explicit(&g_mx_ready, memory_order_acquire)) {
        return;
    }
    hbi_mutex *m = NULL;
    if (hbi_mutex_init(&m) != HBI_OK) {
        return; /* out of memory: claiming will fall back to best-effort */
    }
    bool expected = false;
    /* Publish our mutex only if nobody else has. */
    static atomic_flag creating = ATOMIC_FLAG_INIT;
    if (!atomic_flag_test_and_set_explicit(&creating, memory_order_acquire)) {
        if (!atomic_load_explicit(&g_mx_ready, memory_order_acquire)) {
            g_claim_mx = m;
            atomic_store_explicit(&g_mx_ready, true, memory_order_release);
            m = NULL; /* ownership transferred */
        }
        atomic_flag_clear_explicit(&creating, memory_order_release);
    } else {
        /* Another thread is creating; wait briefly for it to publish. */
        while (!atomic_load_explicit(&g_mx_ready, memory_order_acquire)) {
            hbi_cpu_relax();
        }
    }
    (void)expected;
    if (m != NULL) {
        hbi_mutex_destroy(m); /* we lost the race; discard the spare */
    }
}

/* Find the slot for `name`, or claim a new one. Returns NULL only when the table
 * is full (overflow counted) or a NULL name is passed. `kind` types a new slot. */
static prof_slot *slot_for(const char *name, hbi_prof_kind kind) {
    if (name == NULL) {
        return NULL;
    }
    uint32_t h = fnv1a(name);
    size_t claimed = atomic_load_explicit(&g_slot_count, memory_order_acquire);

    /* Fast path: scan existing claimed slots without locking. */
    for (size_t i = 0; i < claimed; ++i) {
        prof_slot *s = &g_slots[i];
        if (s->hash != h) {
            continue;
        }
        const char *nm = atomic_load_explicit(&s->name, memory_order_acquire);
        if (nm != NULL && strcmp(nm, name) == 0) {
            return s;
        }
    }

    /* Slow path: claim a new slot under the mutex (re-scan to avoid a duplicate
     * claim by a racing thread). */
    ensure_mutex();
    if (atomic_load_explicit(&g_mx_ready, memory_order_acquire)) {
        hbi_mutex_lock(g_claim_mx);
    }
    prof_slot *result = NULL;
    claimed = atomic_load_explicit(&g_slot_count, memory_order_acquire);
    for (size_t i = 0; i < claimed; ++i) {
        prof_slot *s = &g_slots[i];
        if (s->hash == h) {
            const char *nm = atomic_load_explicit(&s->name, memory_order_acquire);
            if (nm != NULL && strcmp(nm, name) == 0) {
                result = s;
                break;
            }
        }
    }
    if (result == NULL) {
        if (claimed < HBI_PROF_MAX_NAMES) {
            prof_slot *s = &g_slots[claimed];
            s->hash = h;
            atomic_store_explicit(&s->kind, (int)kind, memory_order_relaxed);
            atomic_store_explicit(&s->count, 0, memory_order_relaxed);
            atomic_store_explicit(&s->total, 0, memory_order_relaxed);
            atomic_store_explicit(&s->total_ns, 0, memory_order_relaxed);
            atomic_store_explicit(&s->min_ns, UINT64_MAX, memory_order_relaxed);
            atomic_store_explicit(&s->max_ns, 0, memory_order_relaxed);
            atomic_store_explicit(&s->name, name, memory_order_release);
            atomic_store_explicit(&g_slot_count, claimed + 1, memory_order_release);
            result = s;
        } else {
            atomic_fetch_add_explicit(&g_overflow, 1, memory_order_relaxed);
        }
    }
    if (atomic_load_explicit(&g_mx_ready, memory_order_acquire)) {
        hbi_mutex_unlock(g_claim_mx);
    }
    return result;
}

void hbi_prof_set_enabled(bool enabled) {
    atomic_store_explicit(&g_enabled, enabled, memory_order_release);
}

bool hbi_prof_enabled(void) {
    return atomic_load_explicit(&g_enabled, memory_order_acquire);
}

void hbi_prof_reset(void) {
    size_t claimed = atomic_load_explicit(&g_slot_count, memory_order_acquire);
    for (size_t i = 0; i < claimed; ++i) {
        atomic_store_explicit(&g_slots[i].name, NULL, memory_order_relaxed);
    }
    atomic_store_explicit(&g_slot_count, 0, memory_order_release);
    atomic_store_explicit(&g_overflow, 0, memory_order_relaxed);
}

hbi_prof_scope hbi_prof_scope_begin(const char *name) {
    hbi_prof_scope sc;
    sc.name = name;
    sc.start_ns = 0;
    sc.active = false;
    if (atomic_load_explicit(&g_enabled, memory_order_acquire) && name != NULL) {
        sc.start_ns = hbi_time_monotonic_ns();
        sc.active = true;
    }
    return sc;
}

void hbi_prof_scope_end(hbi_prof_scope *scope) {
    if (scope == NULL || !scope->active) {
        return;
    }
    scope->active = false;
    uint64_t end = hbi_time_monotonic_ns();
    uint64_t dur = end >= scope->start_ns ? end - scope->start_ns : 0;

    prof_slot *s = slot_for(scope->name, HBI_PROF_SCOPE);
    if (s == NULL) {
        return;
    }
    atomic_fetch_add_explicit(&s->count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&s->total_ns, dur, memory_order_relaxed);

    /* min/max via CAS loops (contention here is low). */
    uint64_t cur = atomic_load_explicit(&s->min_ns, memory_order_relaxed);
    while (dur < cur && !atomic_compare_exchange_weak_explicit(
                            &s->min_ns, &cur, dur, memory_order_relaxed, memory_order_relaxed)) {
        /* cur reloaded by CAS */
    }
    cur = atomic_load_explicit(&s->max_ns, memory_order_relaxed);
    while (dur > cur && !atomic_compare_exchange_weak_explicit(
                            &s->max_ns, &cur, dur, memory_order_relaxed, memory_order_relaxed)) {
        /* cur reloaded by CAS */
    }
}

void hbi_prof_counter_add(const char *name, int64_t delta) {
    if (!atomic_load_explicit(&g_enabled, memory_order_acquire)) {
        return;
    }
    prof_slot *s = slot_for(name, HBI_PROF_COUNTER);
    if (s == NULL) {
        return;
    }
    atomic_fetch_add_explicit(&s->count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&s->total, delta, memory_order_relaxed);
}

void hbi_prof_event(const char *name, int64_t value) {
    if (!atomic_load_explicit(&g_enabled, memory_order_acquire)) {
        return;
    }
    prof_slot *s = slot_for(name, HBI_PROF_EVENT);
    if (s == NULL) {
        return;
    }
    atomic_fetch_add_explicit(&s->count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&s->total, value, memory_order_relaxed);
}

size_t hbi_prof_count(void) {
    return atomic_load_explicit(&g_slot_count, memory_order_acquire);
}

hbi_status hbi_prof_stat_at(size_t index, hbi_prof_stat *out) {
    if (out == NULL || index >= atomic_load_explicit(&g_slot_count, memory_order_acquire)) {
        return HBI_ERR_INVALID_ARG;
    }
    prof_slot *s = &g_slots[index];
    uint64_t min_ns = atomic_load_explicit(&s->min_ns, memory_order_relaxed);
    out->name = atomic_load_explicit(&s->name, memory_order_acquire);
    out->kind = (hbi_prof_kind)atomic_load_explicit(&s->kind, memory_order_relaxed);
    out->count = atomic_load_explicit(&s->count, memory_order_relaxed);
    out->total = atomic_load_explicit(&s->total, memory_order_relaxed);
    out->total_ns = atomic_load_explicit(&s->total_ns, memory_order_relaxed);
    out->min_ns = (min_ns == UINT64_MAX) ? 0 : min_ns;
    out->max_ns = atomic_load_explicit(&s->max_ns, memory_order_relaxed);
    if (out->name == NULL) {
        out->name = "(unclaimed)";
    }
    return HBI_OK;
}

uint64_t hbi_prof_overflow_count(void) {
    return atomic_load_explicit(&g_overflow, memory_order_relaxed);
}

static const char *kind_str(hbi_prof_kind k) {
    switch (k) {
    case HBI_PROF_SCOPE:
        return "scope";
    case HBI_PROF_COUNTER:
        return "counter";
    case HBI_PROF_EVENT:
        return "event";
    }
    return "?";
}

int hbi_prof_report(char *buf, size_t cap) {
    /* Accumulate into a bounded cursor; snprintf tells us the would-be length so
     * truncation is detectable by the caller. */
    size_t off = 0;
    int total = 0;
    size_t n = hbi_prof_count();

#define EMIT(...)                                                                                  \
    do {                                                                                           \
        int w = snprintf(buf ? buf + off : NULL, (buf && off < cap) ? cap - off : 0, __VA_ARGS__); \
        if (w > 0) {                                                                               \
            total += w;                                                                            \
            off = (size_t)total < cap ? (size_t)total : cap;                                       \
        }                                                                                          \
    } while (0)

    EMIT("profiler report: %zu names, %llu overflow\n", n,
         (unsigned long long)hbi_prof_overflow_count());
    for (size_t i = 0; i < n; ++i) {
        hbi_prof_stat st;
        if (hbi_prof_stat_at(i, &st) != HBI_OK) {
            continue;
        }
        if (st.kind == HBI_PROF_SCOPE) {
            uint64_t avg = st.count ? st.total_ns / st.count : 0;
            EMIT("  %-24s %-7s count=%llu total_ns=%llu avg_ns=%llu "
                 "min_ns=%llu max_ns=%llu\n",
                 st.name, kind_str(st.kind), (unsigned long long)st.count,
                 (unsigned long long)st.total_ns, (unsigned long long)avg,
                 (unsigned long long)st.min_ns, (unsigned long long)st.max_ns);
        } else {
            EMIT("  %-24s %-7s count=%llu total=%lld\n", st.name, kind_str(st.kind),
                 (unsigned long long)st.count, (long long)st.total);
        }
    }
#undef EMIT
    return total;
}

const char *hbi_profiler_name(void) {
    return "profiler";
}

hbi_status hbi_profiler_selftest(void) {
    /* Kind strings must all resolve. */
    if (kind_str(HBI_PROF_SCOPE) == NULL || kind_str(HBI_PROF_COUNTER) == NULL ||
        kind_str(HBI_PROF_EVENT) == NULL) {
        return HBI_ERR_INTERNAL;
    }
    return HBI_OK;
}

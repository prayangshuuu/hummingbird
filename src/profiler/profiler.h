/* profiler.h — Lightweight timing/counter/event instrumentation (§3, PROJECT_CONTEXT
 * principle 3: "measure, don't assume"). This is the machine-readable telemetry
 * stream, deliberately SEPARATE from logging (human diagnostics) — see §3.14.
 *
 * Core-public header for the `profiler` module (layer 2). Symbols are prefixed
 * `hbi_` (internal, no stability guarantee); external embedders use
 * <hummingbird/hummingbird.h>.
 *
 * What it provides:
 *   - SCOPES: named, nestable timing regions (begin/end), aggregated per name
 *     into call-count + total/min/max nanoseconds. A scope guard macro pairs
 *     begin/end so you cannot forget to close one.
 *   - COUNTERS: named monotonic or additive integer tallies (cache hits, bytes
 *     read, ...).
 *   - EVENTS: named instantaneous marks with an optional value (for a future
 *     trace view; recorded as a counter of occurrences here).
 *
 * Cost model: when profiling is DISABLED (the default), every entry point is a
 * single relaxed atomic load + predictable branch — no clock read, no lookup,
 * no allocation. When enabled, a scope costs two monotonic clock reads and a
 * hashed name lookup. Nothing here allocates on the hot path after warm-up:
 * name slots are reserved in a fixed-capacity table.
 *
 * There is NO UI (that is a frontend/dashboard concern). The profiler only
 * accumulates and can dump a plain-text report or hand back raw records.
 *
 * Thread-safety: counters and scope aggregation are safe to call from any
 * thread (per-name slots use atomics). Enabling/disabling and resetting are
 * meant for single-threaded startup/teardown boundaries.
 */
#ifndef HB_PROFILER_H
#define HB_PROFILER_H

#include "common/common.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of distinct instrumentation names (scopes+counters+events)
 * tracked at once. Fixed so the profiler never allocates after startup. A name
 * beyond the cap is dropped (counted in an overflow tally) rather than growing
 * the table on a hot path. */
enum { HBI_PROF_MAX_NAMES = 256 };

/* ── Global enable ───────────────────────────────────────────────────────────
 * Disabled by default: instrumentation compiled into the engine costs almost
 * nothing until a frontend turns it on. Idempotent; safe to toggle at startup. */
void hbi_prof_set_enabled(bool enabled);
bool hbi_prof_enabled(void);

/* Drop all accumulated data and free name slots for reuse. Not thread-safe with
 * concurrent recording — call at a quiescent point. */
void hbi_prof_reset(void);

/* ── Kinds ───────────────────────────────────────────────────────────────────
 * A name slot is typed by first use; mixing kinds on one name is a usage error
 * reported via the common error record (the value is still recorded under the
 * original kind). */
typedef enum hbi_prof_kind {
    HBI_PROF_SCOPE = 0,
    HBI_PROF_COUNTER = 1,
    HBI_PROF_EVENT = 2
} hbi_prof_kind;

/* ── Scopes ──────────────────────────────────────────────────────────────────
 * Time a region. `name` must be a stable string (static literal recommended):
 * it is stored by pointer for identity and by content for the report. Begin
 * returns an opaque token that end consumes; when disabled the token is inert. */
typedef struct hbi_prof_scope {
    const char *name;
    uint64_t start_ns; /* 0 when profiling was disabled at begin */
    bool active;
} hbi_prof_scope;

hbi_prof_scope hbi_prof_scope_begin(const char *name);
void hbi_prof_scope_end(hbi_prof_scope *scope);

/* RAII-style guard for C: opens a scope and (via cleanup attr where available)
 * closes it at block exit. Falls back to a manual begin/end pair the caller must
 * close where the attribute is unsupported. Prefer explicit begin/end in code
 * that must be portable to MSVC. */
#if defined(__GNUC__) || defined(__clang__)
#define HBI_PROF_SCOPE(var, name)                                                                  \
    hbi_prof_scope var __attribute__((cleanup(hbi_prof_scope_end))) = hbi_prof_scope_begin(name)
#else
#define HBI_PROF_SCOPE(var, name) hbi_prof_scope var = hbi_prof_scope_begin(name)
#endif

/* ── Counters & events ───────────────────────────────────────────────────────
 * add: accumulate `delta` into a named counter (may be negative). Event: record
 * one occurrence of `name` (optionally carrying a value for min/max/last). */
void hbi_prof_counter_add(const char *name, int64_t delta);
void hbi_prof_event(const char *name, int64_t value);

/* ── Readback / report ───────────────────────────────────────────────────────
 * A flat snapshot of one name's accumulated stats. For scopes, *_ns fields hold
 * timing; for counters, `total` holds the sum and timing fields are 0. */
typedef struct hbi_prof_stat {
    const char *name;
    hbi_prof_kind kind;
    uint64_t count;    /* scope calls / counter updates / event marks */
    int64_t total;     /* counter/event sum (0 for scopes) */
    uint64_t total_ns; /* scope: summed durations */
    uint64_t min_ns;   /* scope: fastest call */
    uint64_t max_ns;   /* scope: slowest call */
} hbi_prof_stat;

/* Number of live name slots. */
size_t hbi_prof_count(void);

/* Copy the stat at `index` (0..count-1) into *out. Returns HBI_ERR_INVALID_ARG
 * on a bad index/NULL. Snapshot is a point-in-time copy; concurrent updates may
 * race benignly (values are read atomically per field). */
hbi_status hbi_prof_stat_at(size_t index, hbi_prof_stat *out);

/* How many names were dropped because the table was full. */
uint64_t hbi_prof_overflow_count(void);

/* Write a human-readable multi-line report to `buf` (always NUL-terminated when
 * cap>0). Returns the number of bytes that would be written (like snprintf), so
 * truncation is detectable. Ordering is by descending scope total_ns then name. */
int hbi_prof_report(char *buf, size_t cap);

/* ── Module identity / self-test ─────────────────────────────────────────── */
const char *hbi_profiler_name(void);
hbi_status hbi_profiler_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_PROFILER_H */

/* logging.h — Structured, leveled logging. Distinct from telemetry (that is the
 * profiler's job): logging is for human-readable diagnostics, the profiler emits
 * machine-readable timings. Keep the two streams separate (PROJECT_CONTEXT §7).
 *
 * Core-public header for the `logging` module (layer 2). Symbols are prefixed
 * `hbi_` (internal, no stability guarantee). External embedders use
 * <hummingbird/hummingbird.h>.
 *
 * Design:
 *   - Six levels: TRACE, DEBUG, INFO, WARN, ERROR, PROFILING (DD-011/§3.14).
 *   - A record carries level + message + optional structured key/value fields
 *     so a future JSON sink can emit them without reparsing the message.
 *   - Output goes to a pluggable, thread-safe SINK. The default sink writes a
 *     one-line text record to stderr; a JSON sink is a drop-in formatter slot.
 *   - A global minimum level filters cheaply; a compile-time floor
 *     (HB_LOG_COMPILE_LEVEL) lets release builds strip TRACE/DEBUG entirely so
 *     they cost nothing — not even the argument evaluation.
 *
 * Thread-safety: hbi_log_emit and the level getters/setters are thread-safe.
 * Installing a sink (hbi_log_set_sink) is NOT — do it once at startup before
 * other threads log.
 */
#ifndef HB_LOGGING_H
#define HB_LOGGING_H

#include "common/common.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Levels ──────────────────────────────────────────────────────────────────
 * Ordered by increasing severity so a numeric compare implements filtering.
 * PROFILING sits at the top: it is not "more severe" than ERROR, but it is a
 * distinct opt-in stream that should never be filtered out by a severity floor
 * meant to silence chatter — callers select it explicitly. */
typedef enum hbi_log_level {
    HBI_LOG_TRACE = 0,
    HBI_LOG_DEBUG = 1,
    HBI_LOG_INFO = 2,
    HBI_LOG_WARN = 3,
    HBI_LOG_ERROR = 4,
    HBI_LOG_PROFILING = 5,
    HBI_LOG_LEVEL_COUNT /* sentinel */
} hbi_log_level;

/* Stable lower-case spelling ("trace", "info", ...). Never NULL. */
const char *hbi_log_level_str(hbi_log_level level);

/* ── Structured fields ───────────────────────────────────────────────────────
 * A small, fixed-capacity set of key/value pairs attached to one record. Values
 * are pre-formatted strings (the caller owns formatting), keeping the sink
 * interface trivial and allocation-free. */
enum { HBI_LOG_MAX_FIELDS = 8 };

typedef struct hbi_log_field {
    const char *key;   /* static or caller-owned for the emit call's duration */
    const char *value; /* ditto */
} hbi_log_field;

/* ── Record ──────────────────────────────────────────────────────────────────
 * One log event as handed to a sink. All pointers are borrowed for the duration
 * of the sink call only; a sink that needs to retain data must copy it. */
typedef struct hbi_log_record {
    hbi_log_level level;
    uint64_t time_ns;            /* wall-clock nanoseconds at emit */
    uint64_t thread_id;          /* opaque per-thread id */
    const char *file;            /* set-site __FILE__ */
    int line;                    /* set-site __LINE__ */
    const char *message;         /* fully formatted, NUL-terminated */
    const hbi_log_field *fields; /* array of field_count entries (or NULL) */
    size_t field_count;
} hbi_log_record;

/* ── Sink ────────────────────────────────────────────────────────────────────
 * A sink receives fully-built records and renders them. `ctx` is the sink's own
 * state (userdata). A sink must be thread-safe: emit may be called concurrently.
 * The built-in text and JSON sinks serialize internally with a mutex. */
typedef void (*hbi_log_sink_fn)(void *ctx, const hbi_log_record *record);

/* Install the active sink. Passing fn=NULL restores the default text sink.
 * NOT thread-safe — call once at startup. `ctx` is passed back to `fn`. */
void hbi_log_set_sink(hbi_log_sink_fn fn, void *ctx);

/* Built-in sink implementations, exposed so a frontend can select one
 * explicitly (e.g. hbi_log_set_sink(hbi_log_sink_json, stream)). Both are
 * thread-safe. ctx must be a FILE* (or NULL for stderr). */
void hbi_log_sink_text(void *ctx, const hbi_log_record *record);
void hbi_log_sink_json(void *ctx, const hbi_log_record *record);

/* ── Runtime level control ───────────────────────────────────────────────────
 * Records below the active level are dropped before the sink is called. Thread-
 * safe (atomic). Default is HBI_LOG_INFO. PROFILING is never filtered by level
 * (it is an explicit stream, not a severity). */
void hbi_log_set_level(hbi_log_level level);
hbi_log_level hbi_log_get_level(void);

/* True if a record at `level` would currently be emitted. Lets callers skip
 * expensive argument construction. */
bool hbi_log_enabled(hbi_log_level level);

/* ── Emit ────────────────────────────────────────────────────────────────────
 * The core entry point. Prefer the HB_LOG* macros, which capture file/line and
 * short-circuit on the compile-time floor. `fields`/`field_count` may be
 * 0/NULL. printf-style; the formatted message is truncated to an internal cap. */
void hbi_log_emit(hbi_log_level level, const char *file, int line, const hbi_log_field *fields,
                  size_t field_count, const char *fmt, ...) HBI_PRINTF_FMT(6, 7);

/* ── Compile-time floor ──────────────────────────────────────────────────────
 * Levels below HB_LOG_COMPILE_LEVEL are compiled out entirely (the macro
 * expands to nothing), so release builds pay zero cost for TRACE/DEBUG. Define
 * it via the build system; defaults to TRACE (everything compiled in) for Debug
 * and can be raised for Release. */
#ifndef HB_LOG_COMPILE_LEVEL
#define HB_LOG_COMPILE_LEVEL HBI_LOG_TRACE
#endif

#define HB_LOG_AT(level, ...)                                                                      \
    do {                                                                                           \
        if ((level) >= HB_LOG_COMPILE_LEVEL) {                                                     \
            hbi_log_emit((level), __FILE__, __LINE__, NULL, 0, __VA_ARGS__);                       \
        }                                                                                          \
    } while (0)

/* Structured variant: pass a hbi_log_field array + count. */
#define HB_LOG_FIELDS(level, fields, count, ...)                                                   \
    do {                                                                                           \
        if ((level) >= HB_LOG_COMPILE_LEVEL) {                                                     \
            hbi_log_emit((level), __FILE__, __LINE__, (fields), (count), __VA_ARGS__);             \
        }                                                                                          \
    } while (0)

#define HB_LOG_TRACE(...) HB_LOG_AT(HBI_LOG_TRACE, __VA_ARGS__)
#define HB_LOG_DEBUG(...) HB_LOG_AT(HBI_LOG_DEBUG, __VA_ARGS__)
#define HB_LOG_INFO(...) HB_LOG_AT(HBI_LOG_INFO, __VA_ARGS__)
#define HB_LOG_WARN(...) HB_LOG_AT(HBI_LOG_WARN, __VA_ARGS__)
#define HB_LOG_ERROR(...) HB_LOG_AT(HBI_LOG_ERROR, __VA_ARGS__)
#define HB_LOG_PROF(...) HB_LOG_AT(HBI_LOG_PROFILING, __VA_ARGS__)

/* ── Module identity / self-test ─────────────────────────────────────────── */
const char *hbi_logging_name(void);
hbi_status hbi_logging_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_LOGGING_H */

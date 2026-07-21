/* logging.c — leveled, structured logging with a pluggable thread-safe sink.
 *
 * The active level is an atomic so any thread may raise/lower it cheaply. The
 * sink pointer is set once at startup (documented as not thread-safe to change),
 * but the built-in sinks serialize their writes with a mutex so concurrent
 * emits from many threads never interleave a half-line. Message formatting
 * happens on the caller's stack (fixed buffer, no allocation on the log path).
 */
#include "logging/logging_internal.h"

#include "platform/platform.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* Longest formatted message we keep; longer messages are truncated with an
 * ellipsis marker. Lives on the stack of the emitting thread — no allocation. */
enum { HBI_LOG_MSG_CAP = 1024 };

/* ── Level ─────────────────────────────────────────────────────────────────── */

static const char *const g_level_names[HBI_LOG_LEVEL_COUNT] = {
    "trace", "debug", "info", "warn", "error", "profiling",
};

const char *hbi_log_level_str(hbi_log_level level) {
    if (level < 0 || level >= HBI_LOG_LEVEL_COUNT) {
        return "?";
    }
    return g_level_names[level];
}

static atomic_int g_level = HBI_LOG_INFO;

void hbi_log_set_level(hbi_log_level level) {
    if (level < 0 || level >= HBI_LOG_LEVEL_COUNT) {
        return;
    }
    atomic_store_explicit(&g_level, (int)level, memory_order_relaxed);
}

hbi_log_level hbi_log_get_level(void) {
    return (hbi_log_level)atomic_load_explicit(&g_level, memory_order_relaxed);
}

bool hbi_log_enabled(hbi_log_level level) {
    /* PROFILING is an explicit stream, never gated by the severity floor. */
    if (level == HBI_LOG_PROFILING) {
        return true;
    }
    return level >= hbi_log_get_level();
}

/* ── Built-in sinks ──────────────────────────────────────────────────────────
 * A single mutex serializes all built-in sink output. It is created lazily on
 * first use behind a double-checked flag; installing a custom sink that does its
 * own locking bypasses this entirely. */

static hbi_mutex *g_sink_mutex = NULL;
static atomic_bool g_sink_mutex_ready = false;
static hbi_mutex *g_sink_mutex_init_once(void);

static hbi_mutex *sink_mutex(void) {
    if (atomic_load_explicit(&g_sink_mutex_ready, memory_order_acquire)) {
        return g_sink_mutex;
    }
    return g_sink_mutex_init_once();
}

/* Slow path: create the mutex the first time a built-in sink runs. Emits before
 * any threads are spawned (startup logging) are inherently single-threaded, so
 * a plain create-and-publish is sufficient; the release store publishes it. */
static hbi_mutex *g_sink_mutex_init_once(void) {
    if (g_sink_mutex == NULL) {
        hbi_mutex *m = NULL;
        if (hbi_mutex_init(&m) == HBI_OK) {
            g_sink_mutex = m;
        }
    }
    atomic_store_explicit(&g_sink_mutex_ready, true, memory_order_release);
    return g_sink_mutex;
}

static FILE *sink_stream(void *ctx) {
    return ctx != NULL ? (FILE *)ctx : stderr;
}

void hbi_log_sink_text(void *ctx, const hbi_log_record *record) {
    FILE *out = sink_stream(ctx);
    hbi_mutex *m = sink_mutex();
    if (m != NULL) {
        hbi_mutex_lock(m);
    }
    /* level  file:line  tid  message  [k=v ...] */
    fprintf(out, "%-5s %s:%d t%llu  %s", hbi_log_level_str(record->level),
            record->file ? record->file : "?", record->line, (unsigned long long)record->thread_id,
            record->message ? record->message : "");
    for (size_t i = 0; i < record->field_count; ++i) {
        fprintf(out, "  %s=%s", record->fields[i].key ? record->fields[i].key : "?",
                record->fields[i].value ? record->fields[i].value : "");
    }
    fputc('\n', out);
    if (m != NULL) {
        hbi_mutex_unlock(m);
    }
}

/* Escape a string into `buf` per JSON string rules (no surrounding quotes).
 * Always NUL-terminates. Control characters below 0x20 are emitted as \u00XX. */
static void json_escape(const char *s, char *buf, size_t cap) {
    size_t w = 0;
    if (cap == 0) {
        return;
    }
    if (s == NULL) {
        s = "";
    }
    for (; *s != '\0' && w + 7 < cap; ++s) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':
            buf[w++] = '\\';
            buf[w++] = '"';
            break;
        case '\\':
            buf[w++] = '\\';
            buf[w++] = '\\';
            break;
        case '\n':
            buf[w++] = '\\';
            buf[w++] = 'n';
            break;
        case '\r':
            buf[w++] = '\\';
            buf[w++] = 'r';
            break;
        case '\t':
            buf[w++] = '\\';
            buf[w++] = 't';
            break;
        default:
            if (c < 0x20) {
                int n = snprintf(buf + w, cap - w, "\\u%04x", c);
                if (n > 0) {
                    w += (size_t)n;
                }
            } else {
                buf[w++] = (char)c;
            }
            break;
        }
    }
    buf[w] = '\0';
}

void hbi_log_sink_json(void *ctx, const hbi_log_record *record) {
    FILE *out = sink_stream(ctx);
    char esc[HBI_LOG_MSG_CAP * 2];
    hbi_mutex *m = sink_mutex();
    if (m != NULL) {
        hbi_mutex_lock(m);
    }
    json_escape(record->message, esc, sizeof esc);
    fprintf(out,
            "{\"level\":\"%s\",\"time_ns\":%llu,\"tid\":%llu,"
            "\"file\":\"%s\",\"line\":%d,\"msg\":\"%s\"",
            hbi_log_level_str(record->level), (unsigned long long)record->time_ns,
            (unsigned long long)record->thread_id, record->file ? record->file : "?", record->line,
            esc);
    if (record->field_count > 0) {
        fputs(",\"fields\":{", out);
        for (size_t i = 0; i < record->field_count; ++i) {
            json_escape(record->fields[i].value, esc, sizeof esc);
            fprintf(out, "%s\"%s\":\"%s\"", i == 0 ? "" : ",",
                    record->fields[i].key ? record->fields[i].key : "?", esc);
        }
        fputc('}', out);
    }
    fputs("}\n", out);
    if (m != NULL) {
        hbi_mutex_unlock(m);
    }
}

/* ── Active sink ─────────────────────────────────────────────────────────────
 * Set once at startup (see header contract). Read on every emit; a relaxed
 * atomic load is enough because installation happens-before any concurrent log. */
static hbi_log_sink_fn g_sink_fn = hbi_log_sink_text;
static void *g_sink_ctx = NULL;

void hbi_log_set_sink(hbi_log_sink_fn fn, void *ctx) {
    g_sink_fn = fn != NULL ? fn : hbi_log_sink_text;
    g_sink_ctx = ctx;
}

/* ── Emit ────────────────────────────────────────────────────────────────────*/

void hbi_log_emit(hbi_log_level level, const char *file, int line, const hbi_log_field *fields,
                  size_t field_count, const char *fmt, ...) {
    char msg[HBI_LOG_MSG_CAP];
    va_list ap;
    int n;

    if (!hbi_log_enabled(level)) {
        return;
    }
    if (field_count > HBI_LOG_MAX_FIELDS) {
        field_count = HBI_LOG_MAX_FIELDS; /* clamp; never read past the cap */
    }

    if (fmt != NULL) {
        va_start(ap, fmt);
        n = vsnprintf(msg, sizeof msg, fmt, ap);
        va_end(ap);
    } else {
        msg[0] = '\0';
        n = 0;
    }
    if (n < 0) {
        msg[0] = '\0';
    } else if ((size_t)n >= sizeof msg) {
        /* Mark truncation unambiguously. */
        memcpy(msg + sizeof msg - 4, "...", 4);
    }

    hbi_log_record rec = {
        .level = level,
        .time_ns = hbi_time_wall_ns(),
        .thread_id = hbi_thread_current_id(),
        .file = file,
        .line = line,
        .message = msg,
        .fields = field_count > 0 ? fields : NULL,
        .field_count = field_count,
    };
    g_sink_fn(g_sink_ctx, &rec);
}

/* ── Identity / self-test ────────────────────────────────────────────────────*/

const char *hbi_logging_name(void) {
    return "logging";
}

hbi_status hbi_logging_selftest(void) {
    /* Level names are all present and the ordering predicate holds. */
    for (int i = 0; i < HBI_LOG_LEVEL_COUNT; ++i) {
        if (hbi_log_level_str((hbi_log_level)i) == NULL) {
            return HBI_ERR_INTERNAL;
        }
    }
    if (!(HBI_LOG_TRACE < HBI_LOG_INFO && HBI_LOG_INFO < HBI_LOG_ERROR)) {
        return HBI_ERR_INTERNAL;
    }
    return HBI_OK;
}

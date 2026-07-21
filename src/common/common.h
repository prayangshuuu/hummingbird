/* common.h — Foundational types, status codes, error model, and small utilities.
 *
 * Core-public header for the `common` module. Every other core module includes
 * this; it is the single lowest node in the dependency graph (layer 0) and
 * depends on nothing but the C standard library. External embedders use
 * <hummingbird/hummingbird.h> instead. Internal symbols are prefixed `hbi_` and
 * carry no stability guarantee. See docs/architecture/03-dependency-graph.md.
 */
#ifndef HB_COMMON_H
#define HB_COMMON_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── printf-format checking ──────────────────────────────────────────────────
 * Ask the compiler to type-check printf-style variadic args. On MinGW the plain
 * `printf` archetype maps to the MS C runtime, which rejects C99 `%zu`/`%ll`;
 * `gnu_printf` matches the UCRT/ISO conversions the engine actually uses. The
 * (m, n) pair is 1-based: m = format-string arg index, n = first variadic. */
#if defined(__clang__)
#define HBI_PRINTF_FMT(m, n) __attribute__((format(printf, m, n)))
#elif defined(__GNUC__)
#define HBI_PRINTF_FMT(m, n) __attribute__((format(gnu_printf, m, n)))
#else
#define HBI_PRINTF_FMT(m, n)
#endif

/* ── Status codes ──────────────────────────────────────────────────────────
 * Every fallible internal path returns an hbi_status (DD-011). HBI_OK is 0 so
 * `if (status)` reads as "if error". Frontends map these to process exit codes;
 * libhummingbird itself never calls exit(). Keep values stable — they are part
 * of the internal contract. The public ABI mirrors these with its own stable
 * hb_status enum (include/hummingbird/hummingbird.h); hummingbird.c translates.
 */
typedef enum hbi_status {
    HBI_OK = 0,          /* success */
    HBI_ERR_INVALID_ARG, /* caller passed a NULL/out-of-range argument */
    HBI_ERR_OOM,         /* allocation failed */
    HBI_ERR_IO,          /* file/stream I/O failure (see OS error in diag) */
    HBI_ERR_NOT_FOUND,   /* requested entity does not exist */
    HBI_ERR_UNSUPPORTED, /* operation not supported in this build/config */
    HBI_ERR_CORRUPT,     /* malformed on-disk data / failed bounds check */
    HBI_ERR_STATE,       /* called in an invalid lifecycle state */
    HBI_ERR_AGAIN,       /* transient: resource busy / would block / retry */
    HBI_ERR_INTERNAL,    /* invariant violated — a bug */
    HBI_STATUS_COUNT     /* sentinel: number of status codes (not a status) */
} hbi_status;

/* Human-readable, stable spelling of a status code. Never NULL, even for an
 * out-of-range value (returns "HBI_ERR_UNKNOWN"). Safe on any thread. */
const char *hbi_status_str(hbi_status status);

/* ── Error model (DD-011, DD-022) ────────────────────────────────────────────
 * A status code answers "what kind of failure"; the thread-local error record
 * answers "which one, where, and why". Callee code SETS the record at the point
 * of failure with HBI_ERR_SET/HBI_ERR_SETF (capturing file/line/func); callers
 * that want detail READ it with hbi_error_last(). The record is per-thread, so
 * concurrent operations never clobber each other's diagnostics. The OS-errno
 * field is filled with the true platform error (cf. Colibrì #307); the platform
 * module owns turning that number into text — common does not touch OS APIs.
 */
enum { HBI_ERROR_MSG_CAP = 256 }; /* max stored message length, incl. NUL */

typedef struct hbi_error {
    hbi_status status;               /* HBI_OK when no error is recorded */
    int os_errno;                    /* platform error code, or 0 if none */
    const char *file;                /* __FILE__ at the set-site (static) */
    int line;                        /* __LINE__ at the set-site */
    const char *func;                /* __func__ at the set-site (static) */
    char message[HBI_ERROR_MSG_CAP]; /* human-readable, NUL-terminated */
} hbi_error;

/* Pointer to this thread's error record. Never NULL. Valid for the lifetime of
 * the thread; do not free. Reading does not clear it. */
const hbi_error *hbi_error_last(void);

/* Reset this thread's error record to a clean HBI_OK state. */
void hbi_error_clear(void);

/* Record an error on this thread and return `status` (always == the passed
 * status) so call sites can `return hbi_error_set(...)`. `os_errno` is the
 * platform error number to preserve, or 0. `msg` is copied (may be NULL). The
 * file/line/func come from the HBI_ERR_SET* macros. Passing HBI_OK clears the
 * record instead of recording a non-error. */
hbi_status hbi_error_set_at(hbi_status status, int os_errno, const char *file, int line,
                            const char *func, const char *msg);

/* printf-style variant. Same contract as hbi_error_set_at. */
hbi_status hbi_error_setf_at(hbi_status status, int os_errno, const char *file, int line,
                             const char *func, const char *fmt, ...) HBI_PRINTF_FMT(6, 7);

/* Format the current thread's error into `buf` as a single line, e.g.
 *   "HBI_ERR_IO: open failed (os_errno=2) at platform.c:212 in hbi_fs_open"
 * Always NUL-terminates when cap > 0; returns the number of characters that
 * would have been written (like snprintf), so truncation is detectable. */
int hbi_error_format(char *buf, size_t cap);

/* Convenience macros: capture the call-site automatically. Prefer these over
 * calling the _at functions directly. */
#define HBI_ERR_SET(status, os_errno, msg)                                                         \
    hbi_error_set_at((status), (os_errno), __FILE__, __LINE__, __func__, (msg))
#define HBI_ERR_SETF(status, os_errno, ...)                                                        \
    hbi_error_setf_at((status), (os_errno), __FILE__, __LINE__, __func__, __VA_ARGS__)

/* ── Version ───────────────────────────────────────────────────────────────
 * Bootstrap placeholder; the real version is injected by the build system
 * (see cmake/, project() version) once the ABI stabilizes. */
#define HB_VERSION_MAJOR 0
#define HB_VERSION_MINOR 0
#define HB_VERSION_PATCH 0

/* ── Portability helpers ─────────────────────────────────────────────────────
 * Thread-local storage keyword, spelled portably across C11 / GNU / MSVC. Used
 * by the error record and by any module needing per-thread state without a
 * platform dependency (common must stay at layer 0). */
#if defined(_MSC_VER)
#define HBI_THREAD_LOCAL __declspec(thread)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
#define HBI_THREAD_LOCAL _Thread_local
#elif defined(__GNUC__)
#define HBI_THREAD_LOCAL __thread
#else
#error "No thread-local storage keyword available for this compiler"
#endif

/* Branch-prediction hints for cold error paths. No-ops where unsupported. */
#if defined(__GNUC__)
#define HBI_LIKELY(x) __builtin_expect(!!(x), 1)
#define HBI_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define HBI_LIKELY(x) (x)
#define HBI_UNLIKELY(x) (x)
#endif

/* ── Small utilities ─────────────────────────────────────────────────────── */

/* Count of elements in a fixed-size array. */
#define HB_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* Mark an intentionally-unused parameter without a warning. */
#define HB_UNUSED(x) ((void)(x))

/* Smaller / larger of two values (evaluate args once is NOT guaranteed — do not
 * pass expressions with side effects). Kept as macros to stay type-generic in
 * C17 without _Generic noise. */
#define HB_MIN(a, b) ((a) < (b) ? (a) : (b))
#define HB_MAX(a, b) ((a) > (b) ? (a) : (b))

/* Is `x` a power of two (and non-zero)? Useful for alignment checks. */
static inline int hbi_is_pow2(size_t x) {
    return x != 0 && (x & (x - 1)) == 0;
}

/* Round `x` up to the next multiple of `align`, which must be a power of two.
 * Returns 0 on overflow so callers can detect it. */
static inline size_t hbi_align_up(size_t x, size_t align) {
    size_t rounded;
    if (!hbi_is_pow2(align)) {
        return 0;
    }
    rounded = (x + (align - 1)) & ~(align - 1);
    return rounded < x ? 0 : rounded; /* wrapped => overflow */
}

/* Module identity + self-check, mirroring every other core module. */
const char *hbi_common_name(void);
hbi_status hbi_common_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_COMMON_H */

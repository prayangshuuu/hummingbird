/* platform.h — Portability shim: the ONE place OS/compiler/arch differences live.
 *
 * Core-public header for the `platform` module (layer 1). Every OS call —
 * files, threads, mutexes, condition variables, events, timers, CPU query,
 * aligned/huge-page allocation, OS-error translation — is behind this API.
 * The dependency rule is strict (docs/architecture/03-dependency-graph.md):
 * a `#ifdef _WIN32` / `pread` / `mmap` / thread call anywhere under src/ other
 * than src/platform/ is a bug. Everything above this layer is OS-independent.
 *
 * Symbols are prefixed `hbi_` (internal, no stability guarantee). External
 * embedders use <hummingbird/hummingbird.h> instead.
 *
 * Thread-safety: every function here is thread-safe unless its comment says
 * otherwise. The opaque handles (hbi_thread/hbi_mutex/...) are not themselves
 * safe to use concurrently except where noted (a mutex is the whole point).
 */
#ifndef HB_PLATFORM_H
#define HB_PLATFORM_H

#include "common/common.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── OS error translation (DD-022) ──────────────────────────────────────────
 * common records an os_errno at the failure site; platform owns turning that
 * number into human text, because only platform knows the OS convention. */

/* Current thread's last OS error number (errno / GetLastError()). */
int hbi_os_errno(void);

/* Write a human-readable spelling of OS error `code` into `buf` (always
 * NUL-terminated when cap > 0). Returns the length written (excl. NUL). */
size_t hbi_os_strerror(int code, char *buf, size_t cap);

/* ── Aligned & large allocation ─────────────────────────────────────────────
 * The memory module builds its allocators on top of these; low-level code that
 * must allocate before the memory manager exists uses them directly. */

/* Allocate `size` bytes aligned to `alignment` (must be a power of two and a
 * multiple of sizeof(void*)). Returns NULL on failure and sets the OS error.
 * Must be freed with hbi_aligned_free — NOT free(). */
void *hbi_aligned_alloc(size_t alignment, size_t size);

/* Free memory from hbi_aligned_alloc. NULL is a no-op. */
void hbi_aligned_free(void *ptr);

/* Huge-page hint. When `want_huge` is true the allocator REQUESTS large/huge
 * pages; if unavailable it transparently falls back to a normal aligned
 * allocation (never fails only because huge pages are unavailable). On success
 * *out_got_huge (if non-NULL) reports whether huge pages were actually used.
 * Free with hbi_aligned_free. This is a hint today; NUMA binding is a separate
 * concern the memory module layers on later. */
void *hbi_huge_alloc(size_t size, bool want_huge, bool *out_got_huge);

/* System page size in bytes (e.g. 4096). Cached; cheap to call. */
size_t hbi_page_size(void);

/* ── CPU / system topology query ────────────────────────────────────────────
 * A snapshot of the host, filled once and cheap to copy. The device module
 * turns this into its capability report; nothing here interprets it. */
typedef struct hbi_cpu_info {
    int logical_cores;     /* schedulable hardware threads (>= 1) */
    int physical_cores;    /* physical cores, or == logical if unknown */
    size_t page_size;      /* bytes */
    size_t cacheline_size; /* bytes; 64 assumed if the OS won't say */
    char arch[16];         /* "x86_64", "aarch64", "unknown", ... */
} hbi_cpu_info;

/* Fill *out with the host CPU snapshot. Returns HBI_OK, or HBI_ERR_INVALID_ARG
 * if out is NULL. Never fails otherwise — unknown fields get safe defaults. */
hbi_status hbi_cpu_query(hbi_cpu_info *out);

/* ── Time ────────────────────────────────────────────────────────────────────
 * Monotonic clock for durations (never goes backwards, unaffected by wall-clock
 * adjustment); wall clock for timestamps. Both in nanoseconds. */
uint64_t hbi_time_monotonic_ns(void);
uint64_t hbi_time_wall_ns(void);

/* Sleep the current thread for at least `ns` nanoseconds. */
void hbi_sleep_ns(uint64_t ns);

/* ── Threads ─────────────────────────────────────────────────────────────────
 * Opaque handles; create returns an owning handle the caller must join. The
 * entry function takes a void* arg and returns void (results travel through the
 * arg or shared state, matching a work-pool's needs). */
typedef struct hbi_thread hbi_thread;
typedef void (*hbi_thread_fn)(void *arg);

/* Spawn a thread running fn(arg). On success *out receives an owning handle.
 * Returns HBI_ERR_INVALID_ARG (bad args) or HBI_ERR_AGAIN (OS refused). */
hbi_status hbi_thread_create(hbi_thread **out, hbi_thread_fn fn, void *arg);

/* Wait for the thread to finish, release its resources, and free the handle.
 * NULL is a no-op. After this the handle is invalid. */
hbi_status hbi_thread_join(hbi_thread *thread);

/* Opaque, stable-per-thread identifier for logging/debug. Cheap. */
uint64_t hbi_thread_current_id(void);

/* Number of times to spin before blocking — a hint used by hot sync paths. */
void hbi_cpu_relax(void);

/* ── Mutex ───────────────────────────────────────────────────────────────────
 * A plain (non-recursive) mutex. Locking a mutex you already hold is undefined
 * — do not. Zero-initialize is NOT valid; call hbi_mutex_init. */
typedef struct hbi_mutex hbi_mutex;

hbi_status hbi_mutex_init(hbi_mutex **out);
void hbi_mutex_destroy(hbi_mutex *m); /* NULL is a no-op */
void hbi_mutex_lock(hbi_mutex *m);
bool hbi_mutex_trylock(hbi_mutex *m); /* true if acquired */
void hbi_mutex_unlock(hbi_mutex *m);

/* ── Condition variable ──────────────────────────────────────────────────────
 * Paired with a mutex the caller holds across wait. Spurious wakeups are
 * possible — always wait in a predicate loop. */
typedef struct hbi_cond hbi_cond;

hbi_status hbi_cond_init(hbi_cond **out);
void hbi_cond_destroy(hbi_cond *c); /* NULL is a no-op */
void hbi_cond_wait(hbi_cond *c, hbi_mutex *m);
void hbi_cond_signal(hbi_cond *c);    /* wake one waiter */
void hbi_cond_broadcast(hbi_cond *c); /* wake all waiters */

/* ── Event ───────────────────────────────────────────────────────────────────
 * A one-to-many, manual-reset latch: waiters block until it is signalled, then
 * all pass until it is reset. Built on mutex+cond so it is portable and needs
 * no OS event object. Used for "subsystem ready" / "shutdown requested" gates. */
typedef struct hbi_event hbi_event;

hbi_status hbi_event_init(hbi_event **out, bool initially_set);
void hbi_event_destroy(hbi_event *e); /* NULL is a no-op */
void hbi_event_set(hbi_event *e);     /* wake all current + future */
void hbi_event_reset(hbi_event *e);   /* back to unsignalled */
void hbi_event_wait(hbi_event *e);    /* block until set */
bool hbi_event_is_set(hbi_event *e);

/* ── Filesystem ──────────────────────────────────────────────────────────────
 * 64-bit offsets everywhere (mandatory, PROJECT_CONTEXT §7). Binary mode always
 * — no CRLF translation. Positional reads (hbi_file_pread) are thread-safe on
 * the same handle (they do not touch the shared file cursor), which the weight
 * streamer relies on. */
typedef struct hbi_file hbi_file;

typedef enum hbi_file_mode {
    HBI_FILE_READ = 0,  /* open existing for reading */
    HBI_FILE_WRITE = 1, /* create/truncate for writing */
    HBI_FILE_RDWR = 2   /* create if absent, open read+write, no truncate */
} hbi_file_mode;

hbi_status hbi_file_open(hbi_file **out, const char *path, hbi_file_mode mode);
hbi_status hbi_file_close(hbi_file *f); /* NULL is a no-op */

/* Sequential read/write via the file cursor. *out_n receives bytes moved
 * (0 at EOF for read). Not thread-safe on one handle (shared cursor). */
hbi_status hbi_file_read(hbi_file *f, void *buf, size_t n, size_t *out_n);
hbi_status hbi_file_write(hbi_file *f, const void *buf, size_t n, size_t *out_n);

/* Positional read: read up to `n` bytes at absolute `offset`. Does not move or
 * depend on the cursor, so it is safe to call concurrently on one handle from
 * many threads. *out_n receives bytes read (0 at/after EOF). */
hbi_status hbi_file_pread(hbi_file *f, void *buf, size_t n, uint64_t offset, size_t *out_n);

hbi_status hbi_file_size(hbi_file *f, uint64_t *out_size);

/* Path helpers (no handle needed). */
bool hbi_path_exists(const char *path);
hbi_status hbi_path_remove(const char *path); /* HBI_ERR_NOT_FOUND if absent */

/* ── Module identity / self-test (mirrors every core module) ──────────────── */
const char *hbi_platform_name(void);
hbi_status hbi_platform_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_PLATFORM_H */

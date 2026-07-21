/* platform.c — the single implementation of the OS abstraction (layer 1).
 *
 * All OS-specific code in the engine lives here, behind `#ifdef _WIN32` /
 * POSIX branches. The rule (docs/architecture/03-dependency-graph.md) is that
 * no other file under src/ contains an OS ifdef or a raw syscall. The two
 * branches implement the exact same platform.h contract with identical
 * semantics; higher layers cannot tell which one they are running on.
 *
 * Concurrency primitives (mutex/cond) wrap the native objects. Threads wrap the
 * native thread. Events are built portably on mutex+cond (no native event
 * object), so their behavior is bit-identical on every OS.
 */
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform/platform_internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach_time.h>
#include <sys/sysctl.h>
#endif
#endif

/* Architecture string, decided at compile time. */
#if defined(__x86_64__) || defined(_M_X64)
#define HBI_ARCH_STR "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define HBI_ARCH_STR "aarch64"
#elif defined(__i386__) || defined(_M_IX86)
#define HBI_ARCH_STR "x86"
#elif defined(__arm__) || defined(_M_ARM)
#define HBI_ARCH_STR "arm"
#else
#define HBI_ARCH_STR "unknown"
#endif

/* ══════════════════════════════════════════════════════════════════════════
 * OS error translation (DD-022)
 * ════════════════════════════════════════════════════════════════════════ */

int hbi_os_errno(void) {
#if defined(_WIN32)
    return (int)GetLastError();
#else
    return errno;
#endif
}

size_t hbi_os_strerror(int code, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return 0;
    }
#if defined(_WIN32)
    {
        DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                                 (DWORD)code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf,
                                 (DWORD)cap, NULL);
        if (n == 0) {
            int w = snprintf(buf, cap, "OS error %d", code);
            return (w < 0) ? 0 : (size_t)w;
        }
        /* Trim trailing CR/LF/period that FormatMessage appends. */
        while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == '.' ||
                         buf[n - 1] == ' ')) {
            buf[--n] = '\0';
        }
        return (size_t)n;
    }
#else
    /* strerror_r has two incompatible signatures; snprintf(strerror) is not
     * thread-safe, so use the portable XSI/GNU handling via a local copy. */
    {
        const char *s = strerror(code);
        int w = snprintf(buf, cap, "%s", s ? s : "unknown error");
        return (w < 0) ? 0 : (size_t)w;
    }
#endif
}

/* ══════════════════════════════════════════════════════════════════════════
 * Aligned & huge-page allocation
 * ════════════════════════════════════════════════════════════════════════ */

void *hbi_aligned_alloc(size_t alignment, size_t size) {
    if (!hbi_is_pow2(alignment) || alignment < sizeof(void *)) {
        return NULL;
    }
    if (size == 0) {
        size = 1; /* return a unique, freeable pointer */
    }
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    {
        void *p = NULL;
        /* posix_memalign requires size need not be a multiple of alignment. */
        if (posix_memalign(&p, alignment, size) != 0) {
            return NULL;
        }
        return p;
    }
#endif
}

void hbi_aligned_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

void *hbi_huge_alloc(size_t size, bool want_huge, bool *out_got_huge) {
    bool got_huge = false;
    void *p = NULL;

    if (want_huge) {
#if defined(_WIN32)
        SIZE_T large = GetLargePageMinimum();
        if (large != 0) {
            size_t rounded = hbi_align_up(size, (size_t)large);
            if (rounded != 0) {
                p = VirtualAlloc(NULL, rounded, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
                                 PAGE_READWRITE);
                if (p != NULL) {
                    got_huge = true;
                }
            }
        }
#elif defined(MADV_HUGEPAGE)
        /* Transparent huge pages: allocate page-aligned and advise. The kernel
         * may or may not honor it; we report best-effort, not a guarantee. */
        size_t pg = hbi_page_size();
        p = hbi_aligned_alloc(pg < sizeof(void *) ? sizeof(void *) : pg, size);
        if (p != NULL && madvise(p, size, MADV_HUGEPAGE) == 0) {
            got_huge = true;
        }
#endif
    }

    if (p == NULL) {
        /* Fallback: ordinary aligned allocation (never fails just for lack of
         * huge pages). Align to the page size for locality. */
        size_t pg = hbi_page_size();
        size_t al = pg < sizeof(void *) ? sizeof(void *) : pg;
        p = hbi_aligned_alloc(al, size);
        got_huge = false;
    }

    if (out_got_huge != NULL) {
        *out_got_huge = got_huge;
    }
    return p;
}

size_t hbi_page_size(void) {
    static size_t cached = 0;
    if (cached != 0) {
        return cached;
    }
#if defined(_WIN32)
    {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        cached = (size_t)si.dwPageSize;
    }
#else
    {
        long v = sysconf(_SC_PAGESIZE);
        cached = (v > 0) ? (size_t)v : 4096u;
    }
#endif
    if (cached == 0) {
        cached = 4096u;
    }
    return cached;
}

/* ══════════════════════════════════════════════════════════════════════════
 * CPU / system query
 * ════════════════════════════════════════════════════════════════════════ */

hbi_status hbi_cpu_query(hbi_cpu_info *out) {
    if (out == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->page_size = hbi_page_size();
    out->cacheline_size = 64;
    snprintf(out->arch, sizeof(out->arch), "%s", HBI_ARCH_STR);

#if defined(_WIN32)
    {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        out->logical_cores = (int)si.dwNumberOfProcessors;
        out->physical_cores = out->logical_cores; /* refined below if possible */

        /* Physical core + cache-line detection via processor info. */
        DWORD len = 0;
        GetLogicalProcessorInformation(NULL, &len);
        if (len > 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION *buf =
                (SYSTEM_LOGICAL_PROCESSOR_INFORMATION *)malloc(len);
            if (buf != NULL && GetLogicalProcessorInformation(buf, &len)) {
                int phys = 0;
                DWORD count = len / (DWORD)sizeof(*buf);
                for (DWORD i = 0; i < count; ++i) {
                    if (buf[i].Relationship == RelationProcessorCore) {
                        phys++;
                    } else if (buf[i].Relationship == RelationCache && buf[i].Cache.Level == 1) {
                        out->cacheline_size = buf[i].Cache.LineSize;
                    }
                }
                if (phys > 0) {
                    out->physical_cores = phys;
                }
            }
            free(buf);
        }
    }
#else
    {
        long v = sysconf(_SC_NPROCESSORS_ONLN);
        out->logical_cores = (v > 0) ? (int)v : 1;
        out->physical_cores = out->logical_cores;
#if defined(__APPLE__)
        {
            int phys = 0;
            size_t sz = sizeof(phys);
            if (sysctlbyname("hw.physicalcpu", &phys, &sz, NULL, 0) == 0 && phys > 0) {
                out->physical_cores = phys;
            }
            int64_t line = 0;
            sz = sizeof(line);
            if (sysctlbyname("hw.cachelinesize", &line, &sz, NULL, 0) == 0 && line > 0) {
                out->cacheline_size = (size_t)line;
            }
        }
#endif
    }
#endif
    if (out->logical_cores < 1) {
        out->logical_cores = 1;
    }
    if (out->physical_cores < 1) {
        out->physical_cores = out->logical_cores;
    }
    if (out->cacheline_size == 0) {
        out->cacheline_size = 64;
    }
    return HBI_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Time
 * ════════════════════════════════════════════════════════════════════════ */

uint64_t hbi_time_monotonic_ns(void) {
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    static int have_freq = 0;
    LARGE_INTEGER now;
    if (!have_freq) {
        QueryPerformanceFrequency(&freq);
        have_freq = 1;
    }
    QueryPerformanceCounter(&now);
    /* Scale to ns without overflow: (ticks / freq) * 1e9. */
    {
        uint64_t ticks = (uint64_t)now.QuadPart;
        uint64_t f = (uint64_t)freq.QuadPart;
        uint64_t secs = ticks / f;
        uint64_t rem = ticks % f;
        return secs * 1000000000ull + (rem * 1000000000ull) / f;
    }
#elif defined(__APPLE__)
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0) {
        mach_timebase_info(&tb);
    }
    return mach_absolute_time() * tb.numer / tb.denom;
#else
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    }
#endif
}

uint64_t hbi_time_wall_ns(void) {
#if defined(_WIN32)
    {
        FILETIME ft;
        ULARGE_INTEGER u;
        GetSystemTimePreciseAsFileTime(&ft);
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        /* FILETIME is 100-ns ticks since 1601-01-01; shift epoch to 1970. */
        const uint64_t EPOCH_DIFF_100NS = 116444736000000000ull;
        uint64_t t = u.QuadPart - EPOCH_DIFF_100NS;
        return t * 100ull;
    }
#else
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    }
#endif
}

void hbi_sleep_ns(uint64_t ns) {
#if defined(_WIN32)
    /* Sleep granularity is ms; round up so we never sleep less than asked. */
    DWORD ms = (DWORD)((ns + 999999ull) / 1000000ull);
    Sleep(ms);
#else
    {
        struct timespec ts;
        ts.tv_sec = (time_t)(ns / 1000000000ull);
        ts.tv_nsec = (long)(ns % 1000000000ull);
        while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
            /* resume the remaining time */
        }
    }
#endif
}

void hbi_cpu_relax(void) {
#if defined(_WIN32)
    YieldProcessor();
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ __volatile__("pause");
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield");
#else
    /* nothing portable; a compiler barrier keeps the spin honest */
    __asm__ __volatile__("" ::: "memory");
#endif
}

/* ══════════════════════════════════════════════════════════════════════════
 * Threads
 * ════════════════════════════════════════════════════════════════════════ */

struct hbi_thread {
    hbi_thread_fn fn;
    void *arg;
#if defined(_WIN32)
    HANDLE handle;
#else
    pthread_t handle;
#endif
};

#if defined(_WIN32)
static DWORD WINAPI hbi_thread_trampoline(LPVOID p) {
    hbi_thread *t = (hbi_thread *)p;
    t->fn(t->arg);
    return 0;
}
#else
static void *hbi_thread_trampoline(void *p) {
    hbi_thread *t = (hbi_thread *)p;
    t->fn(t->arg);
    return NULL;
}
#endif

hbi_status hbi_thread_create(hbi_thread **out, hbi_thread_fn fn, void *arg) {
    if (out == NULL || fn == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
    *out = NULL;
    hbi_thread *t = (hbi_thread *)calloc(1, sizeof(*t));
    if (t == NULL) {
        return HBI_ERR_OOM;
    }
    t->fn = fn;
    t->arg = arg;
#if defined(_WIN32)
    t->handle = CreateThread(NULL, 0, hbi_thread_trampoline, t, 0, NULL);
    if (t->handle == NULL) {
        int e = hbi_os_errno();
        free(t);
        return HBI_ERR_SET(HBI_ERR_AGAIN, e, "CreateThread failed");
    }
#else
    {
        int rc = pthread_create(&t->handle, NULL, hbi_thread_trampoline, t);
        if (rc != 0) {
            free(t);
            return HBI_ERR_SET(HBI_ERR_AGAIN, rc, "pthread_create failed");
        }
    }
#endif
    *out = t;
    return HBI_OK;
}

hbi_status hbi_thread_join(hbi_thread *t) {
    if (t == NULL) {
        return HBI_OK;
    }
#if defined(_WIN32)
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
#else
    pthread_join(t->handle, NULL);
#endif
    free(t);
    return HBI_OK;
}

uint64_t hbi_thread_current_id(void) {
#if defined(_WIN32)
    return (uint64_t)GetCurrentThreadId();
#elif defined(__APPLE__)
    {
        uint64_t tid = 0;
        pthread_threadid_np(NULL, &tid);
        return tid;
    }
#else
    return (uint64_t)(uintptr_t)pthread_self();
#endif
}

/* ══════════════════════════════════════════════════════════════════════════
 * Mutex
 * ════════════════════════════════════════════════════════════════════════ */

struct hbi_mutex {
#if defined(_WIN32)
    CRITICAL_SECTION cs;
#else
    pthread_mutex_t m;
#endif
};

hbi_status hbi_mutex_init(hbi_mutex **out) {
    if (out == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
    hbi_mutex *m = (hbi_mutex *)calloc(1, sizeof(*m));
    if (m == NULL) {
        return HBI_ERR_OOM;
    }
#if defined(_WIN32)
    InitializeCriticalSection(&m->cs);
#else
    if (pthread_mutex_init(&m->m, NULL) != 0) {
        free(m);
        return HBI_ERR_INTERNAL;
    }
#endif
    *out = m;
    return HBI_OK;
}

void hbi_mutex_destroy(hbi_mutex *m) {
    if (m == NULL) {
        return;
    }
#if defined(_WIN32)
    DeleteCriticalSection(&m->cs);
#else
    pthread_mutex_destroy(&m->m);
#endif
    free(m);
}

void hbi_mutex_lock(hbi_mutex *m) {
#if defined(_WIN32)
    EnterCriticalSection(&m->cs);
#else
    pthread_mutex_lock(&m->m);
#endif
}

bool hbi_mutex_trylock(hbi_mutex *m) {
#if defined(_WIN32)
    return TryEnterCriticalSection(&m->cs) != 0;
#else
    return pthread_mutex_trylock(&m->m) == 0;
#endif
}

void hbi_mutex_unlock(hbi_mutex *m) {
#if defined(_WIN32)
    LeaveCriticalSection(&m->cs);
#else
    pthread_mutex_unlock(&m->m);
#endif
}

/* ══════════════════════════════════════════════════════════════════════════
 * Condition variable
 * ════════════════════════════════════════════════════════════════════════ */

struct hbi_cond {
#if defined(_WIN32)
    CONDITION_VARIABLE cv;
#else
    pthread_cond_t cv;
#endif
};

hbi_status hbi_cond_init(hbi_cond **out) {
    if (out == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
    hbi_cond *c = (hbi_cond *)calloc(1, sizeof(*c));
    if (c == NULL) {
        return HBI_ERR_OOM;
    }
#if defined(_WIN32)
    InitializeConditionVariable(&c->cv);
#else
    if (pthread_cond_init(&c->cv, NULL) != 0) {
        free(c);
        return HBI_ERR_INTERNAL;
    }
#endif
    *out = c;
    return HBI_OK;
}

void hbi_cond_destroy(hbi_cond *c) {
    if (c == NULL) {
        return;
    }
#if !defined(_WIN32)
    pthread_cond_destroy(&c->cv);
#endif
    free(c);
}

void hbi_cond_wait(hbi_cond *c, hbi_mutex *m) {
#if defined(_WIN32)
    SleepConditionVariableCS(&c->cv, &m->cs, INFINITE);
#else
    pthread_cond_wait(&c->cv, &m->m);
#endif
}

void hbi_cond_signal(hbi_cond *c) {
#if defined(_WIN32)
    WakeConditionVariable(&c->cv);
#else
    pthread_cond_signal(&c->cv);
#endif
}

void hbi_cond_broadcast(hbi_cond *c) {
#if defined(_WIN32)
    WakeAllConditionVariable(&c->cv);
#else
    pthread_cond_broadcast(&c->cv);
#endif
}

/* ══════════════════════════════════════════════════════════════════════════
 * Event — portable manual-reset latch on mutex + cond
 * ════════════════════════════════════════════════════════════════════════ */

struct hbi_event {
    hbi_mutex *m;
    hbi_cond *c;
    bool is_set;
};

hbi_status hbi_event_init(hbi_event **out, bool initially_set) {
    if (out == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
    hbi_event *e = (hbi_event *)calloc(1, sizeof(*e));
    if (e == NULL) {
        return HBI_ERR_OOM;
    }
    hbi_status s = hbi_mutex_init(&e->m);
    if (s != HBI_OK) {
        free(e);
        return s;
    }
    s = hbi_cond_init(&e->c);
    if (s != HBI_OK) {
        hbi_mutex_destroy(e->m);
        free(e);
        return s;
    }
    e->is_set = initially_set;
    *out = e;
    return HBI_OK;
}

void hbi_event_destroy(hbi_event *e) {
    if (e == NULL) {
        return;
    }
    hbi_cond_destroy(e->c);
    hbi_mutex_destroy(e->m);
    free(e);
}

void hbi_event_set(hbi_event *e) {
    hbi_mutex_lock(e->m);
    e->is_set = true;
    hbi_cond_broadcast(e->c);
    hbi_mutex_unlock(e->m);
}

void hbi_event_reset(hbi_event *e) {
    hbi_mutex_lock(e->m);
    e->is_set = false;
    hbi_mutex_unlock(e->m);
}

void hbi_event_wait(hbi_event *e) {
    hbi_mutex_lock(e->m);
    while (!e->is_set) {
        hbi_cond_wait(e->c, e->m);
    }
    hbi_mutex_unlock(e->m);
}

bool hbi_event_is_set(hbi_event *e) {
    hbi_mutex_lock(e->m);
    bool v = e->is_set;
    hbi_mutex_unlock(e->m);
    return v;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Filesystem
 * ════════════════════════════════════════════════════════════════════════ */

struct hbi_file {
#if defined(_WIN32)
    HANDLE h;
#else
    int fd;
#endif
};

hbi_status hbi_file_open(hbi_file **out, const char *path, hbi_file_mode mode) {
    if (out == NULL || path == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
    *out = NULL;
    hbi_file *f = (hbi_file *)calloc(1, sizeof(*f));
    if (f == NULL) {
        return HBI_ERR_OOM;
    }
#if defined(_WIN32)
    {
        DWORD access = 0, disp = 0, share = FILE_SHARE_READ;
        switch (mode) {
        case HBI_FILE_READ:
            access = GENERIC_READ;
            disp = OPEN_EXISTING;
            break;
        case HBI_FILE_WRITE:
            access = GENERIC_WRITE;
            disp = CREATE_ALWAYS;
            break;
        case HBI_FILE_RDWR:
            access = GENERIC_READ | GENERIC_WRITE;
            disp = OPEN_ALWAYS;
            break;
        default:
            free(f);
            return HBI_ERR_INVALID_ARG;
        }
        f->h = CreateFileA(path, access, share, NULL, disp, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f->h == INVALID_HANDLE_VALUE) {
            int e = hbi_os_errno();
            hbi_status st = (e == (int)ERROR_FILE_NOT_FOUND || e == (int)ERROR_PATH_NOT_FOUND)
                                ? HBI_ERR_NOT_FOUND
                                : HBI_ERR_IO;
            free(f);
            return HBI_ERR_SETF(st, e, "open '%s' failed", path);
        }
    }
#else
    {
        int flags = 0;
        switch (mode) {
        case HBI_FILE_READ:
            flags = O_RDONLY;
            break;
        case HBI_FILE_WRITE:
            flags = O_WRONLY | O_CREAT | O_TRUNC;
            break;
        case HBI_FILE_RDWR:
            flags = O_RDWR | O_CREAT;
            break;
        default:
            free(f);
            return HBI_ERR_INVALID_ARG;
        }
#if defined(O_BINARY)
        flags |= O_BINARY; /* no-op on real POSIX; explicit for clarity */
#endif
        f->fd = open(path, flags, 0644);
        if (f->fd < 0) {
            int e = errno;
            hbi_status st = (e == ENOENT) ? HBI_ERR_NOT_FOUND : HBI_ERR_IO;
            free(f);
            return HBI_ERR_SETF(st, e, "open '%s' failed", path);
        }
    }
#endif
    *out = f;
    return HBI_OK;
}

hbi_status hbi_file_close(hbi_file *f) {
    if (f == NULL) {
        return HBI_OK;
    }
#if defined(_WIN32)
    CloseHandle(f->h);
#else
    close(f->fd);
#endif
    free(f);
    return HBI_OK;
}

hbi_status hbi_file_read(hbi_file *f, void *buf, size_t n, size_t *out_n) {
    if (f == NULL || (buf == NULL && n != 0)) {
        return HBI_ERR_INVALID_ARG;
    }
    if (out_n != NULL) {
        *out_n = 0;
    }
#if defined(_WIN32)
    {
        DWORD got = 0;
        if (!ReadFile(f->h, buf, (DWORD)n, &got, NULL)) {
            return HBI_ERR_SET(HBI_ERR_IO, hbi_os_errno(), "ReadFile failed");
        }
        if (out_n != NULL) {
            *out_n = (size_t)got;
        }
    }
#else
    {
        ssize_t got = read(f->fd, buf, n);
        if (got < 0) {
            return HBI_ERR_SET(HBI_ERR_IO, errno, "read failed");
        }
        if (out_n != NULL) {
            *out_n = (size_t)got;
        }
    }
#endif
    return HBI_OK;
}

hbi_status hbi_file_write(hbi_file *f, const void *buf, size_t n, size_t *out_n) {
    if (f == NULL || (buf == NULL && n != 0)) {
        return HBI_ERR_INVALID_ARG;
    }
    if (out_n != NULL) {
        *out_n = 0;
    }
#if defined(_WIN32)
    {
        DWORD put = 0;
        if (!WriteFile(f->h, buf, (DWORD)n, &put, NULL)) {
            return HBI_ERR_SET(HBI_ERR_IO, hbi_os_errno(), "WriteFile failed");
        }
        if (out_n != NULL) {
            *out_n = (size_t)put;
        }
    }
#else
    {
        ssize_t put = write(f->fd, buf, n);
        if (put < 0) {
            return HBI_ERR_SET(HBI_ERR_IO, errno, "write failed");
        }
        if (out_n != NULL) {
            *out_n = (size_t)put;
        }
    }
#endif
    return HBI_OK;
}

hbi_status hbi_file_pread(hbi_file *f, void *buf, size_t n, uint64_t offset, size_t *out_n) {
    if (f == NULL || (buf == NULL && n != 0)) {
        return HBI_ERR_INVALID_ARG;
    }
    if (out_n != NULL) {
        *out_n = 0;
    }
#if defined(_WIN32)
    {
        OVERLAPPED ov;
        DWORD got = 0;
        memset(&ov, 0, sizeof(ov));
        ov.Offset = (DWORD)(offset & 0xFFFFFFFFull);
        ov.OffsetHigh = (DWORD)(offset >> 32);
        if (!ReadFile(f->h, buf, (DWORD)n, &got, &ov)) {
            int e = hbi_os_errno();
            if (e == (int)ERROR_HANDLE_EOF) {
                return HBI_OK; /* read past EOF => 0 bytes, not an error */
            }
            return HBI_ERR_SET(HBI_ERR_IO, e, "positional ReadFile failed");
        }
        if (out_n != NULL) {
            *out_n = (size_t)got;
        }
    }
#else
    {
        ssize_t got = pread(f->fd, buf, n, (off_t)offset);
        if (got < 0) {
            return HBI_ERR_SET(HBI_ERR_IO, errno, "pread failed");
        }
        if (out_n != NULL) {
            *out_n = (size_t)got;
        }
    }
#endif
    return HBI_OK;
}

hbi_status hbi_file_size(hbi_file *f, uint64_t *out_size) {
    if (f == NULL || out_size == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
#if defined(_WIN32)
    {
        LARGE_INTEGER sz;
        if (!GetFileSizeEx(f->h, &sz)) {
            return HBI_ERR_SET(HBI_ERR_IO, hbi_os_errno(), "GetFileSizeEx failed");
        }
        *out_size = (uint64_t)sz.QuadPart;
    }
#else
    {
        struct stat st;
        if (fstat(f->fd, &st) != 0) {
            return HBI_ERR_SET(HBI_ERR_IO, errno, "fstat failed");
        }
        *out_size = (uint64_t)st.st_size;
    }
#endif
    return HBI_OK;
}

bool hbi_path_exists(const char *path) {
    if (path == NULL) {
        return false;
    }
#if defined(_WIN32)
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    return access(path, F_OK) == 0;
#endif
}

hbi_status hbi_path_remove(const char *path) {
    if (path == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
#if defined(_WIN32)
    if (!DeleteFileA(path)) {
        int e = hbi_os_errno();
        hbi_status st = (e == (int)ERROR_FILE_NOT_FOUND) ? HBI_ERR_NOT_FOUND : HBI_ERR_IO;
        return HBI_ERR_SETF(st, e, "remove '%s' failed", path);
    }
#else
    if (remove(path) != 0) {
        int e = errno;
        hbi_status st = (e == ENOENT) ? HBI_ERR_NOT_FOUND : HBI_ERR_IO;
        return HBI_ERR_SETF(st, e, "remove '%s' failed", path);
    }
#endif
    return HBI_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Module identity / self-test
 * ════════════════════════════════════════════════════════════════════════ */

const char *hbi_platform_name(void) {
    return "platform";
}

hbi_status hbi_platform_selftest(void) {
    /* Page size is sane. */
    size_t pg = hbi_page_size();
    if (pg < 512 || !hbi_is_pow2(pg)) {
        return HBI_ERR_INTERNAL;
    }
    /* CPU query fills sane defaults. */
    hbi_cpu_info info;
    if (hbi_cpu_query(&info) != HBI_OK || info.logical_cores < 1 || info.cacheline_size == 0) {
        return HBI_ERR_INTERNAL;
    }
    /* Monotonic clock does not run backwards. */
    uint64_t t0 = hbi_time_monotonic_ns();
    uint64_t t1 = hbi_time_monotonic_ns();
    if (t1 < t0) {
        return HBI_ERR_INTERNAL;
    }
    /* Aligned alloc honors alignment and is freeable. */
    void *p = hbi_aligned_alloc(64, 1000);
    if (p == NULL || ((uintptr_t)p & 63u) != 0) {
        hbi_aligned_free(p);
        return HBI_ERR_INTERNAL;
    }
    hbi_aligned_free(p);
    return HBI_OK;
}

/* platform_test.c — unit tests for the OS abstraction layer.
 *
 * Exercises every subsystem the shim exposes: aligned/huge allocation, CPU
 * query, both clocks, threads, mutex, condition variable, event latch, and
 * file I/O (including positional reads). Uses a real temp file under the OS
 * temp dir via a relative path in the build tree. */
#include "platform/platform.h"

#include "hbi_test.h"

#include <stdint.h>
#include <string.h>

static void test_identity(void) {
    HBI_CHECK_STR_EQ(hbi_platform_name(), "platform");
    HBI_CHECK(hbi_platform_selftest() == HBI_OK);
}

static void test_aligned_alloc(void) {
    for (size_t align = sizeof(void *); align <= 4096; align <<= 1) {
        void *p = hbi_aligned_alloc(align, 1000);
        HBI_CHECK_MSG(p != NULL, "aligned_alloc(%zu) returned NULL", align);
        if (p != NULL) {
            HBI_CHECK_MSG(((uintptr_t)p % align) == 0, "misaligned for align=%zu", align);
            /* Memory must be writable across the whole request. */
            memset(p, 0xAB, 1000);
            hbi_aligned_free(p);
        }
    }
    /* Non-power-of-two alignment is rejected, not undefined. */
    HBI_CHECK(hbi_aligned_alloc(48, 64) == NULL);
    hbi_aligned_free(NULL); /* NULL free is a no-op */
}

static void test_huge_alloc(void) {
    bool got_huge = true;
    void *p = hbi_huge_alloc(2u * 1024 * 1024, true, &got_huge);
    /* Huge pages may be unavailable; the call must still succeed with a
     * normal allocation and report got_huge=false rather than failing. */
    HBI_CHECK(p != NULL);
    if (p != NULL) {
        memset(p, 0, 4096);
        hbi_aligned_free(p);
    }
    /* Explicitly not-huge must also work. */
    p = hbi_huge_alloc(4096, false, &got_huge);
    HBI_CHECK(p != NULL);
    HBI_CHECK(got_huge == false);
    hbi_aligned_free(p);
}

static void test_cpu_query(void) {
    hbi_cpu_info info;
    HBI_CHECK(hbi_cpu_query(&info) == HBI_OK);
    HBI_CHECK(info.logical_cores >= 1);
    HBI_CHECK(info.physical_cores >= 1);
    HBI_CHECK(info.page_size >= 512);
    HBI_CHECK(hbi_is_pow2(info.page_size));
    HBI_CHECK(info.cacheline_size >= 16);
    HBI_CHECK(info.arch[0] != '\0');
    HBI_CHECK(hbi_cpu_query(NULL) == HBI_ERR_INVALID_ARG);
    HBI_CHECK(hbi_page_size() == info.page_size);
}

static void test_time(void) {
    uint64_t a = hbi_time_monotonic_ns();
    hbi_sleep_ns(2u * 1000 * 1000); /* 2 ms */
    uint64_t b = hbi_time_monotonic_ns();
    HBI_CHECK_MSG(b > a, "monotonic clock did not advance (%llu -> %llu)", (unsigned long long)a,
                  (unsigned long long)b);
    HBI_CHECK(hbi_time_wall_ns() > 0);
}

/* ── Threads + synchronization ──────────────────────────────────────────── */

typedef struct {
    hbi_mutex *mtx;
    long long counter;
    int iters;
} shared_counter;

static void adder_thread(void *arg) {
    shared_counter *s = (shared_counter *)arg;
    for (int i = 0; i < s->iters; ++i) {
        hbi_mutex_lock(s->mtx);
        s->counter += 1;
        hbi_mutex_unlock(s->mtx);
    }
}

static void test_threads_mutex(void) {
    shared_counter s;
    s.counter = 0;
    s.iters = 100000;
    HBI_CHECK(hbi_mutex_init(&s.mtx) == HBI_OK);

    enum { N = 4 };
    hbi_thread *threads[N];
    int spawned = 0;
    for (int i = 0; i < N; ++i) {
        if (hbi_thread_create(&threads[i], adder_thread, &s) == HBI_OK) {
            ++spawned;
        } else {
            threads[i] = NULL;
        }
    }
    HBI_CHECK(spawned == N);
    for (int i = 0; i < N; ++i) {
        hbi_thread_join(threads[i]);
    }
    HBI_CHECK_EQ_INT(s.counter, (long long)N * s.iters);

    HBI_CHECK(hbi_mutex_trylock(s.mtx));
    hbi_mutex_unlock(s.mtx);
    hbi_mutex_destroy(s.mtx);
    HBI_CHECK(hbi_thread_current_id() != 0);
}

/* Condition-variable handoff: producer sets a flag, consumer waits on it. */
typedef struct {
    hbi_mutex *mtx;
    hbi_cond *cv;
    int ready;
    int value;
} handoff;

static void producer_thread(void *arg) {
    handoff *h = (handoff *)arg;
    hbi_mutex_lock(h->mtx);
    h->value = 777;
    h->ready = 1;
    hbi_cond_signal(h->cv);
    hbi_mutex_unlock(h->mtx);
}

static void test_condvar(void) {
    handoff h;
    h.ready = 0;
    h.value = 0;
    HBI_CHECK(hbi_mutex_init(&h.mtx) == HBI_OK);
    HBI_CHECK(hbi_cond_init(&h.cv) == HBI_OK);

    hbi_thread *t = NULL;
    HBI_CHECK(hbi_thread_create(&t, producer_thread, &h) == HBI_OK);

    hbi_mutex_lock(h.mtx);
    while (!h.ready) {
        hbi_cond_wait(h.cv, h.mtx);
    }
    hbi_mutex_unlock(h.mtx);
    HBI_CHECK_EQ_INT(h.value, 777);

    hbi_thread_join(t);
    hbi_cond_destroy(h.cv);
    hbi_mutex_destroy(h.mtx);
}

/* Event latch: worker blocks until the main thread sets the event. */
typedef struct {
    hbi_event *ev;
    hbi_mutex *mtx;
    int passed;
} gate;

static void waiter_thread(void *arg) {
    gate *g = (gate *)arg;
    hbi_event_wait(g->ev);
    hbi_mutex_lock(g->mtx);
    g->passed = 1;
    hbi_mutex_unlock(g->mtx);
}

static void test_event(void) {
    gate g;
    g.passed = 0;
    HBI_CHECK(hbi_mutex_init(&g.mtx) == HBI_OK);
    HBI_CHECK(hbi_event_init(&g.ev, false) == HBI_OK);
    HBI_CHECK(hbi_event_is_set(g.ev) == false);

    hbi_thread *t = NULL;
    HBI_CHECK(hbi_thread_create(&t, waiter_thread, &g) == HBI_OK);

    /* Give the waiter a moment to block, then release it. */
    hbi_sleep_ns(5u * 1000 * 1000);
    hbi_event_set(g.ev);
    hbi_thread_join(t);

    HBI_CHECK(g.passed == 1);
    HBI_CHECK(hbi_event_is_set(g.ev) == true);

    hbi_event_reset(g.ev);
    HBI_CHECK(hbi_event_is_set(g.ev) == false);

    hbi_event_destroy(g.ev);
    hbi_mutex_destroy(g.mtx);
}

/* ── Filesystem ─────────────────────────────────────────────────────────── */

static void test_files(void) {
    const char *path = "hb_platform_test.tmp";
    const char payload[] = "Hummingbird platform I/O test payload.";
    const size_t len = sizeof(payload) - 1;

    hbi_path_remove(path); /* clean any leftover; ignore result */

    hbi_file *f = NULL;
    HBI_CHECK(hbi_file_open(&f, path, HBI_FILE_WRITE) == HBI_OK);
    if (f != NULL) {
        size_t wrote = 0;
        HBI_CHECK(hbi_file_write(f, payload, len, &wrote) == HBI_OK);
        HBI_CHECK_EQ_INT(wrote, (long long)len);
        HBI_CHECK(hbi_file_close(f) == HBI_OK);
    }

    HBI_CHECK(hbi_path_exists(path));

    HBI_CHECK(hbi_file_open(&f, path, HBI_FILE_READ) == HBI_OK);
    if (f != NULL) {
        uint64_t size = 0;
        HBI_CHECK(hbi_file_size(f, &size) == HBI_OK);
        HBI_CHECK_EQ_INT(size, (long long)len);

        char buf[64];
        size_t got = 0;
        HBI_CHECK(hbi_file_read(f, buf, sizeof(buf), &got) == HBI_OK);
        HBI_CHECK_EQ_INT(got, (long long)len);
        HBI_CHECK(memcmp(buf, payload, len) == 0);

        /* Positional read of a slice, independent of the cursor. */
        char slice[6];
        size_t pgot = 0;
        HBI_CHECK(hbi_file_pread(f, slice, 5, 0, &pgot) == HBI_OK);
        HBI_CHECK_EQ_INT(pgot, 5);
        HBI_CHECK(memcmp(slice, "Hummi", 5) == 0);

        /* Read past EOF yields zero bytes, not an error. */
        HBI_CHECK(hbi_file_pread(f, slice, 5, size + 100, &pgot) == HBI_OK);
        HBI_CHECK_EQ_INT(pgot, 0);

        HBI_CHECK(hbi_file_close(f) == HBI_OK);
    }

    HBI_CHECK(hbi_path_remove(path) == HBI_OK);
    HBI_CHECK(!hbi_path_exists(path));
    /* Removing a now-absent path reports NOT_FOUND. */
    HBI_CHECK(hbi_path_remove(path) == HBI_ERR_NOT_FOUND);

    /* Opening a missing file for read fails cleanly with a recorded error. */
    HBI_CHECK(hbi_file_open(&f, "definitely_not_here.xyz", HBI_FILE_READ) != HBI_OK);
}

static void test_os_error(void) {
    char buf[128];
    size_t n = hbi_os_strerror(0, buf, sizeof(buf));
    HBI_CHECK(n > 0);
    HBI_CHECK(buf[0] != '\0');
    /* Zero-cap must not write, must not crash. */
    HBI_CHECK(hbi_os_strerror(0, NULL, 0) == 0);
}

int main(void) {
    HBI_TEST_BEGIN("platform");
    HBI_RUN(test_identity);
    HBI_RUN(test_aligned_alloc);
    HBI_RUN(test_huge_alloc);
    HBI_RUN(test_cpu_query);
    HBI_RUN(test_time);
    HBI_RUN(test_threads_mutex);
    HBI_RUN(test_condvar);
    HBI_RUN(test_event);
    HBI_RUN(test_files);
    HBI_RUN(test_os_error);
    return HBI_TEST_END();
}

/* threadpool_test.c — unit tests for the reusable worker pool.
 *
 * Covers: lifecycle, single- and multi-worker execution, back-pressure and
 * non-blocking submit, wait-for-quiescence, reuse after wait, introspection,
 * and clean shutdown with work still queued. Concurrency is exercised by
 * summing a large number of tasks that each atomically bump a shared counter.
 */
#include "threadpool/threadpool.h"

#include "hbi_test.h"

#include <stdatomic.h>
#include <stdint.h>

static atomic_int g_counter;

static void bump_task(void *arg) {
    (void)arg;
    atomic_fetch_add_explicit(&g_counter, 1, memory_order_relaxed);
}

typedef struct sum_arg {
    atomic_llong *total;
    long value;
} sum_arg;

static void add_task(void *arg) {
    sum_arg *s = (sum_arg *)arg;
    atomic_fetch_add_explicit(s->total, s->value, memory_order_relaxed);
}

static void test_create_destroy(void) {
    hbi_threadpool *pool = NULL;
    HBI_CHECK_EQ_INT(hbi_threadpool_create(&pool, 4, 16), HBI_OK);
    HBI_CHECK(pool != NULL);
    HBI_CHECK_EQ_INT(hbi_threadpool_worker_count(pool), 4);
    HBI_CHECK_EQ_INT((long long)hbi_threadpool_completed_count(pool), 0);
    hbi_threadpool_destroy(pool);

    /* NULL out is rejected; NULL destroy is a no-op. */
    HBI_CHECK_EQ_INT(hbi_threadpool_create(NULL, 2, 8), HBI_ERR_INVALID_ARG);
    hbi_threadpool_destroy(NULL);
}

static void test_worker_clamp(void) {
    /* Zero/negative workers clamp to 1; zero queue clamps to 1. */
    hbi_threadpool *pool = NULL;
    HBI_CHECK_EQ_INT(hbi_threadpool_create(&pool, 0, 0), HBI_OK);
    HBI_CHECK(hbi_threadpool_worker_count(pool) >= 1);
    hbi_threadpool_destroy(pool);
}

static void test_runs_all_tasks(void) {
    atomic_store(&g_counter, 0);
    hbi_threadpool *pool = NULL;
    HBI_CHECK_EQ_INT(hbi_threadpool_create(&pool, 4, 64), HBI_OK);

    const int N = 1000;
    for (int i = 0; i < N; ++i) {
        HBI_CHECK_EQ_INT(hbi_threadpool_submit(pool, bump_task, NULL), HBI_OK);
    }
    hbi_threadpool_wait(pool);
    HBI_CHECK_EQ_INT(atomic_load(&g_counter), N);
    HBI_CHECK_EQ_INT((long long)hbi_threadpool_completed_count(pool), N);

    hbi_threadpool_destroy(pool);
}

static void test_concurrent_sum(void) {
    /* Each task adds its own value; the total must be exact regardless of order. */
    atomic_llong total;
    atomic_store(&total, 0);
    hbi_threadpool *pool = NULL;
    HBI_CHECK_EQ_INT(hbi_threadpool_create(&pool, 8, 32), HBI_OK);

    enum { N = 500 };
    static sum_arg args[N];
    long expected = 0;
    for (int i = 0; i < N; ++i) {
        args[i].total = &total;
        args[i].value = i;
        expected += i;
        HBI_CHECK_EQ_INT(hbi_threadpool_submit(pool, add_task, &args[i]), HBI_OK);
    }
    hbi_threadpool_wait(pool);
    HBI_CHECK_EQ_INT(atomic_load(&total), expected);
    hbi_threadpool_destroy(pool);
}

static void test_reuse_after_wait(void) {
    atomic_store(&g_counter, 0);
    hbi_threadpool *pool = NULL;
    HBI_CHECK_EQ_INT(hbi_threadpool_create(&pool, 2, 8), HBI_OK);

    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 10; ++i) {
            HBI_CHECK_EQ_INT(hbi_threadpool_submit(pool, bump_task, NULL), HBI_OK);
        }
        hbi_threadpool_wait(pool);
    }
    HBI_CHECK_EQ_INT(atomic_load(&g_counter), 30);
    hbi_threadpool_destroy(pool);
}

static void test_try_submit_backpressure(void) {
    /* A tiny queue with a single slow-ish worker: try_submit must eventually
     * report HBI_ERR_AGAIN when the queue saturates, never block. */
    atomic_store(&g_counter, 0);
    hbi_threadpool *pool = NULL;
    HBI_CHECK_EQ_INT(hbi_threadpool_create(&pool, 1, 2), HBI_OK);

    int again_seen = 0;
    int accepted = 0;
    for (int i = 0; i < 10000; ++i) {
        hbi_status st = hbi_threadpool_try_submit(pool, bump_task, NULL);
        if (st == HBI_OK) {
            ++accepted;
        } else if (st == HBI_ERR_AGAIN) {
            again_seen = 1;
            break;
        } else {
            HBI_CHECK_MSG(0, "unexpected try_submit status %d", (int)st);
            break;
        }
    }
    HBI_CHECK(again_seen); /* back-pressure was actually exercised */
    HBI_CHECK(accepted >= 1);

    hbi_threadpool_wait(pool);
    hbi_threadpool_destroy(pool);
}

static void test_submit_after_shutdown_rejected(void) {
    /* Not a use-after-free test: we verify NULL args are rejected up front. */
    HBI_CHECK_EQ_INT(hbi_threadpool_submit(NULL, bump_task, NULL), HBI_ERR_INVALID_ARG);

    hbi_threadpool *pool = NULL;
    HBI_CHECK_EQ_INT(hbi_threadpool_create(&pool, 2, 8), HBI_OK);
    HBI_CHECK_EQ_INT(hbi_threadpool_submit(pool, NULL, NULL), HBI_ERR_INVALID_ARG);
    hbi_threadpool_destroy(pool);
}

static void test_destroy_drains_queued_work(void) {
    /* destroy waits for all queued tasks to finish, so the counter must be full. */
    atomic_store(&g_counter, 0);
    hbi_threadpool *pool = NULL;
    HBI_CHECK_EQ_INT(hbi_threadpool_create(&pool, 4, 512), HBI_OK);
    for (int i = 0; i < 200; ++i) {
        HBI_CHECK_EQ_INT(hbi_threadpool_submit(pool, bump_task, NULL), HBI_OK);
    }
    hbi_threadpool_destroy(pool); /* must drain, not drop */
    HBI_CHECK_EQ_INT(atomic_load(&g_counter), 200);
}

static void test_identity(void) {
    HBI_CHECK_EQ_INT(hbi_threadpool_selftest(), HBI_OK);
    HBI_CHECK_STR_EQ(hbi_threadpool_name(), "threadpool");
}

int main(void) {
    HBI_TEST_BEGIN("threadpool");
    HBI_RUN(test_create_destroy);
    HBI_RUN(test_worker_clamp);
    HBI_RUN(test_runs_all_tasks);
    HBI_RUN(test_concurrent_sum);
    HBI_RUN(test_reuse_after_wait);
    HBI_RUN(test_try_submit_backpressure);
    HBI_RUN(test_submit_after_shutdown_rejected);
    HBI_RUN(test_destroy_drains_queued_work);
    HBI_RUN(test_identity);
    return HBI_TEST_END();
}

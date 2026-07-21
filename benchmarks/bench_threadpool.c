/* bench_threadpool.c — micro-benchmark for the thread pool (threadpool module).
 *
 * Measures task-dispatch throughput: how fast the pool can accept and complete a
 * large number of trivial tasks. Internal benchmark (links hb_threadpool
 * directly) for the same reason as bench_alloc.c — the pool has no public ABI.
 *
 * The task itself is intentionally tiny (increment an atomic) so the number
 * reflects dispatch/synchronization overhead, not task work.
 */
#include "platform/platform.h"
#include "threadpool/threadpool.h"

#include <stdatomic.h>
#include <stdio.h>

static atomic_ullong g_counter;

static void tiny_task(void *arg) {
    (void)arg;
    atomic_fetch_add_explicit(&g_counter, 1, memory_order_relaxed);
}

static double bench_workers(int workers, int tasks) {
    hbi_threadpool *pool = NULL;
    if (hbi_threadpool_create(&pool, workers, 1024) != HBI_OK) {
        return 0.0;
    }
    atomic_store_explicit(&g_counter, 0, memory_order_relaxed);

    uint64_t start = hbi_time_monotonic_ns();
    for (int i = 0; i < tasks; ++i) {
        (void)hbi_threadpool_submit(pool, tiny_task, NULL);
    }
    hbi_threadpool_wait(pool);
    uint64_t elapsed = hbi_time_monotonic_ns() - start;

    unsigned long long done = atomic_load_explicit(&g_counter, memory_order_relaxed);
    hbi_threadpool_destroy(pool);

    if (done != (unsigned long long)tasks) {
        fprintf(stderr, "  WARNING: completed %llu of %d tasks\n", done, tasks);
    }
    return (double)elapsed / (double)tasks; /* ns per task */
}

int main(void) {
    const int task_count = 500000;
    const int worker_counts[] = {1, 2, 4, 8};

    printf("thread pool micro-benchmark (%d tasks each)\n", task_count);
    printf("%10s %14s %16s\n", "workers", "ns/task", "tasks/sec");
    for (size_t i = 0; i < HB_ARRAY_LEN(worker_counts); ++i) {
        double ns = bench_workers(worker_counts[i], task_count);
        double per_sec = ns > 0.0 ? 1e9 / ns : 0.0;
        printf("%10d %14.2f %16.0f\n", worker_counts[i], ns, per_sec);
    }
    return 0;
}

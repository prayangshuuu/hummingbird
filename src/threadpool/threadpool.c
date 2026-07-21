/* threadpool.c — fixed-size worker pool over the platform thread/mutex/cond shim.
 *
 * Design (deliberately simple — DD-020):
 *   - A bounded ring-buffer task queue guarded by one mutex.
 *   - Two condition variables: `not_empty` wakes idle workers when work arrives;
 *     `not_full` wakes blocked submitters when a slot frees. A third condition,
 *     `idle`, wakes hbi_threadpool_wait when the queue drains AND no task is in
 *     flight.
 *   - Workers loop: wait for a task, run it, mark it done. Shutdown is a flag the
 *     workers observe; destroy drains first so no submitted task is dropped.
 *
 * Correctness invariants:
 *   - Every submitted task runs exactly once (queue is FIFO, single dequeue point
 *     under the lock).
 *   - `pending` counts queued-but-not-yet-finished tasks (incremented at enqueue,
 *     decremented after the task returns), so `wait` blocks until it hits zero.
 *   - The pool never calls exit()/abort(): a bad task is the caller's contract.
 */
#include "threadpool/threadpool_internal.h"

#include "platform/platform.h"

#include <stdatomic.h>
#include <stdlib.h>

typedef struct task {
    hbi_task_fn fn;
    void *arg;
} task;

struct hbi_threadpool {
    hbi_mutex *mtx;
    hbi_cond *not_empty; /* signalled when a task is enqueued */
    hbi_cond *not_full;  /* signalled when a task is dequeued */
    hbi_cond *idle;      /* signalled when pending hits 0 */

    task *queue; /* ring buffer of `capacity` slots */
    size_t capacity;
    size_t head;    /* next slot to dequeue */
    size_t count;   /* slots currently occupied */
    size_t pending; /* enqueued but not yet finished (>= count) */

    hbi_thread **workers;
    int num_workers;
    bool shutting_down;

    atomic_ullong completed; /* tasks finished, for introspection */
};

/* Worker main loop. Runs until shutdown is requested AND the queue is empty. */
static void worker_main(void *arg) {
    hbi_threadpool *pool = (hbi_threadpool *)arg;

    for (;;) {
        hbi_mutex_lock(pool->mtx);
        while (pool->count == 0 && !pool->shutting_down) {
            hbi_cond_wait(pool->not_empty, pool->mtx);
        }
        if (pool->count == 0 && pool->shutting_down) {
            hbi_mutex_unlock(pool->mtx);
            return; /* nothing left and we are closing */
        }

        /* Dequeue one task. */
        task t = pool->queue[pool->head];
        pool->head = (pool->head + 1) % pool->capacity;
        pool->count -= 1;
        hbi_cond_signal(pool->not_full);
        hbi_mutex_unlock(pool->mtx);

        /* Run outside the lock so tasks execute concurrently. */
        t.fn(t.arg);
        atomic_fetch_add_explicit(&pool->completed, 1, memory_order_relaxed);

        /* Mark completion; wake wait() if we just drained the pool. */
        hbi_mutex_lock(pool->mtx);
        pool->pending -= 1;
        if (pool->pending == 0) {
            hbi_cond_broadcast(pool->idle);
        }
        hbi_mutex_unlock(pool->mtx);
    }
}

static void destroy_sync_objects(hbi_threadpool *pool) {
    hbi_cond_destroy(pool->idle);
    hbi_cond_destroy(pool->not_full);
    hbi_cond_destroy(pool->not_empty);
    hbi_mutex_destroy(pool->mtx);
}

hbi_status hbi_threadpool_create(hbi_threadpool **out, int num_workers, size_t queue_capacity) {
    if (out == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (num_workers < 1) {
        num_workers = 1;
    }
    if (queue_capacity < 1) {
        queue_capacity = 1;
    }

    hbi_threadpool *pool = (hbi_threadpool *)calloc(1, sizeof(*pool));
    if (pool == NULL) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "threadpool: struct allocation failed");
    }
    pool->capacity = queue_capacity;
    pool->num_workers = num_workers;
    atomic_store_explicit(&pool->completed, 0, memory_order_relaxed);

    pool->queue = (task *)calloc(queue_capacity, sizeof(task));
    pool->workers = (hbi_thread **)calloc((size_t)num_workers, sizeof(hbi_thread *));
    if (pool->queue == NULL || pool->workers == NULL) {
        free(pool->workers);
        free(pool->queue);
        free(pool);
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "threadpool: queue/worker allocation failed");
    }

    /* Sync objects. */
    if (hbi_mutex_init(&pool->mtx) != HBI_OK || hbi_cond_init(&pool->not_empty) != HBI_OK ||
        hbi_cond_init(&pool->not_full) != HBI_OK || hbi_cond_init(&pool->idle) != HBI_OK) {
        destroy_sync_objects(pool);
        free(pool->workers);
        free(pool->queue);
        free(pool);
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "threadpool: sync object init failed");
    }

    /* Spawn workers. If one fails, unwind the ones already started. */
    for (int i = 0; i < num_workers; ++i) {
        hbi_status st = hbi_thread_create(&pool->workers[i], worker_main, pool);
        if (st != HBI_OK) {
            /* Ask the started workers to exit, join them, then tear down. */
            hbi_mutex_lock(pool->mtx);
            pool->shutting_down = true;
            hbi_cond_broadcast(pool->not_empty);
            hbi_mutex_unlock(pool->mtx);
            for (int j = 0; j < i; ++j) {
                hbi_thread_join(pool->workers[j]);
            }
            destroy_sync_objects(pool);
            free(pool->workers);
            free(pool->queue);
            free(pool);
            return HBI_ERR_SET(HBI_ERR_AGAIN, 0, "threadpool: worker thread creation failed");
        }
    }

    *out = pool;
    return HBI_OK;
}

void hbi_threadpool_destroy(hbi_threadpool *pool) {
    if (pool == NULL) {
        return;
    }
    /* Signal shutdown; workers finish the queue then exit. */
    hbi_mutex_lock(pool->mtx);
    pool->shutting_down = true;
    hbi_cond_broadcast(pool->not_empty);
    hbi_mutex_unlock(pool->mtx);

    for (int i = 0; i < pool->num_workers; ++i) {
        hbi_thread_join(pool->workers[i]);
    }

    destroy_sync_objects(pool);
    free(pool->workers);
    free(pool->queue);
    free(pool);
}

/* Shared enqueue core. When `block` is false and the queue is full, returns
 * HBI_ERR_AGAIN without waiting. Caller must pass non-NULL pool/fn. */
static hbi_status enqueue(hbi_threadpool *pool, hbi_task_fn fn, void *arg, bool block) {
    hbi_mutex_lock(pool->mtx);

    if (pool->shutting_down) {
        hbi_mutex_unlock(pool->mtx);
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "threadpool: submit after shutdown");
    }

    while (pool->count == pool->capacity) {
        if (!block) {
            hbi_mutex_unlock(pool->mtx);
            return HBI_ERR_AGAIN;
        }
        hbi_cond_wait(pool->not_full, pool->mtx);
        if (pool->shutting_down) {
            hbi_mutex_unlock(pool->mtx);
            return HBI_ERR_SET(HBI_ERR_STATE, 0, "threadpool: shutdown while blocked on submit");
        }
    }

    size_t tail = (pool->head + pool->count) % pool->capacity;
    pool->queue[tail].fn = fn;
    pool->queue[tail].arg = arg;
    pool->count += 1;
    pool->pending += 1;
    hbi_cond_signal(pool->not_empty);
    hbi_mutex_unlock(pool->mtx);
    return HBI_OK;
}

hbi_status hbi_threadpool_submit(hbi_threadpool *pool, hbi_task_fn fn, void *arg) {
    if (pool == NULL || fn == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
    return enqueue(pool, fn, arg, true);
}

hbi_status hbi_threadpool_try_submit(hbi_threadpool *pool, hbi_task_fn fn, void *arg) {
    if (pool == NULL || fn == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
    return enqueue(pool, fn, arg, false);
}

void hbi_threadpool_wait(hbi_threadpool *pool) {
    if (pool == NULL) {
        return;
    }
    hbi_mutex_lock(pool->mtx);
    while (pool->pending > 0) {
        hbi_cond_wait(pool->idle, pool->mtx);
    }
    hbi_mutex_unlock(pool->mtx);
}

int hbi_threadpool_worker_count(const hbi_threadpool *pool) {
    return pool ? pool->num_workers : 0;
}

uint64_t hbi_threadpool_completed_count(const hbi_threadpool *pool) {
    if (pool == NULL) {
        return 0;
    }
    return atomic_load_explicit(&pool->completed, memory_order_relaxed);
}

const char *hbi_threadpool_name(void) {
    return "threadpool";
}

hbi_status hbi_threadpool_selftest(void) {
    /* A tiny pool must run a task and reach quiescence. */
    hbi_threadpool *pool = NULL;
    if (hbi_threadpool_create(&pool, 2, 8) != HBI_OK) {
        return HBI_ERR_INTERNAL;
    }
    hbi_threadpool_wait(pool);
    hbi_threadpool_destroy(pool);
    return HBI_OK;
}

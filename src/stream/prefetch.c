#include "stream/stream.h"
#include "threadpool/threadpool.h"
#include <stdlib.h>
#include <string.h>

struct hbi_prefetch_engine_t {
    hbi_scheduler_t *sched;
    hbi_threadpool *pool;
};

hbi_prefetch_engine_t *hbi_prefetch_engine_create(hbi_scheduler_t *sched) {
    hbi_prefetch_engine_t *prefetch = calloc(1, sizeof(hbi_prefetch_engine_t));
    if (!prefetch)
        return NULL;
    prefetch->sched = sched;

    // Create a prefetch background thread pool (2 workers, 256 queue)
    if (hbi_threadpool_create(&prefetch->pool, 2, 256) != HBI_OK) {
        free(prefetch);
        return NULL;
    }

    return prefetch;
}

void hbi_prefetch_engine_destroy(hbi_prefetch_engine_t *prefetch) {
    if (prefetch) {
        if (prefetch->pool) {
            hbi_threadpool_destroy(prefetch->pool);
        }
        free(prefetch);
    }
}

typedef struct {
    hbi_scheduler_t *sched;
    hbi_block_id_t id;
    size_t size;
} prefetch_task_t;

static void prefetch_worker_fn(void *arg) {
    prefetch_task_t *task = (prefetch_task_t *)arg;
    hbi_scheduler_prefetch(task->sched, task->id, task->size);
    free(task);
}

void hbi_prefetch_engine_submit(hbi_prefetch_engine_t *prefetch, hbi_block_id_t *sequence,
                                size_t count, size_t size_per_block) {
    if (!prefetch || !prefetch->sched || !prefetch->pool)
        return;

    for (size_t i = 0; i < count; i++) {
        prefetch_task_t *task = malloc(sizeof(prefetch_task_t));
        if (task) {
            task->sched = prefetch->sched;
            task->id = sequence[i];
            task->size = size_per_block;

            // Non-blocking submission: drop prefetch if queue is full
            if (hbi_threadpool_try_submit(prefetch->pool, prefetch_worker_fn, task) != HBI_OK) {
                free(task);
            }
        }
    }
}

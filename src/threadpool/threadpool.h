/* threadpool.h — a reusable fixed-size worker pool for generic work execution
 * (DD-012, DD-020). This is the ONE thread-pool abstraction the engine builds on;
 * matmul parallelism, the weight-loader pool, and prefetch will all be expressed
 * as tasks submitted here rather than hand-rolling threads (the Colibrì lesson:
 * "portability/concurrency lives in one place", PROJECT_CONTEXT §2.19).
 *
 * Core-public header for the `threadpool` module (layer 2). Symbols are prefixed
 * `hbi_` (internal, no stability guarantee); external embedders use
 * <hummingbird/hummingbird.h>.
 *
 * Scope for this phase: submit independent tasks, wait for quiescence, shut down
 * cleanly. NO scheduling policy, priorities, work-stealing, or dependency graph —
 * those belong to the `scheduler` module (layer 8) that will sit on top. Keeping
 * this layer dumb is deliberate: it is a correctness-simple primitive.
 *
 * Threads are built on the platform shim (hbi_thread/hbi_mutex/hbi_cond), so this
 * module contains no OS calls of its own.
 *
 * Error model: the pool NEVER calls exit() and never aborts on a task (DD-011). A
 * task function that misbehaves is the caller's contract; the pool only guarantees
 * each submitted task runs exactly once on some worker.
 */
#ifndef HB_THREADPOOL_H
#define HB_THREADPOOL_H

#include "common/common.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A unit of work: fn(arg) runs on a worker thread. Results travel through `arg`
 * (e.g. a struct the caller owns) or other shared state the caller synchronizes.
 * A task must not assume anything about which worker runs it or in what order
 * relative to other tasks. */
typedef void (*hbi_task_fn)(void *arg);

/* Opaque pool handle. */
typedef struct hbi_threadpool hbi_threadpool;

/* ── Lifecycle ───────────────────────────────────────────────────────────────
 * Create a pool with `num_workers` threads (clamped to >= 1) and an internal
 * task queue of `queue_capacity` pending tasks (clamped to >= 1). On success
 * *out receives an owning handle. Returns HBI_ERR_INVALID_ARG (NULL out),
 * HBI_ERR_OOM, or HBI_ERR_AGAIN (the OS refused a thread). */
hbi_status hbi_threadpool_create(hbi_threadpool **out, int num_workers, size_t queue_capacity);

/* Wait for all queued + in-flight tasks to finish, stop the workers, and free
 * the pool. NULL is a no-op. After this the handle is invalid. Safe to call once
 * from the owner thread; do not submit concurrently with destroy. */
void hbi_threadpool_destroy(hbi_threadpool *pool);

/* ── Submission ──────────────────────────────────────────────────────────────
 * Enqueue fn(arg). If the queue is full this BLOCKS until space frees up (back-
 * pressure) rather than dropping work or growing unboundedly. Returns
 * HBI_ERR_INVALID_ARG (NULL pool/fn) or HBI_ERR_STATE (pool is shutting down). */
hbi_status hbi_threadpool_submit(hbi_threadpool *pool, hbi_task_fn fn, void *arg);

/* Non-blocking variant: enqueue only if space is immediately available.
 * Returns HBI_ERR_AGAIN if the queue is full, otherwise as hbi_threadpool_submit. */
hbi_status hbi_threadpool_try_submit(hbi_threadpool *pool, hbi_task_fn fn, void *arg);

/* ── Synchronization ─────────────────────────────────────────────────────────
 * Block until every task submitted before this call has completed and the queue
 * is empty. Does NOT stop the pool — it can be reused after. Tasks submitted by
 * other threads concurrently with wait have undefined inclusion; use it from the
 * owner after a batch of submits. */
void hbi_threadpool_wait(hbi_threadpool *pool);

/* ── Introspection ───────────────────────────────────────────────────────────
 * Cheap, lock-free-ish snapshots for tests, telemetry, and doctor output. */
int hbi_threadpool_worker_count(const hbi_threadpool *pool);
uint64_t hbi_threadpool_completed_count(const hbi_threadpool *pool);

/* ── Module identity / self-test ─────────────────────────────────────────── */
const char *hbi_threadpool_name(void);
hbi_status hbi_threadpool_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_THREADPOOL_H */

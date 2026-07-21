# `threadpool` module

A reusable, fixed-size worker pool for generic work execution (ADR DD-012, DD-020).

## Status

Implemented (Phase 5 — runtime foundation). This is the single thread-pool
abstraction the engine builds on; higher layers (`scheduler`) add policy on top.
No scheduling policy, priorities, work-stealing, or task dependencies live here
by design — this is a correctness-simple primitive.

## What it provides

- **Lifecycle:** `hbi_threadpool_create` / `hbi_threadpool_destroy` — a pool of
  N workers over a bounded task queue. Destroy drains all work, then stops.
- **Submission:** `hbi_threadpool_submit` (blocks with back-pressure when the
  queue is full) and `hbi_threadpool_try_submit` (non-blocking, `HBI_ERR_AGAIN`
  when full).
- **Synchronization:** `hbi_threadpool_wait` blocks until the queue is empty and
  every in-flight task has completed; the pool is reusable afterward.
- **Introspection:** worker count and cumulative completed-task count.

Threads, mutexes, and condition variables come from the `platform` shim, so this
module contains no OS calls of its own. It never calls `exit()` and never aborts
on a misbehaving task (ADR DD-011).

## Layout

| File | Role |
|------|------|
| `threadpool.h` | Core-public header (`hbi_threadpool_*`). |
| `threadpool_internal.h` | Private structs (queue, worker state). |
| `threadpool.c` | Implementation. |
| `threadpool_test.c` | Unit tests (CTest target `threadpool`). |
| `CMakeLists.txt` | Build target `hb_threadpool`. |

## Allowed dependencies

This module may depend **only** on: `common`, `platform`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. In particular it must not
depend on `logging`/`config`/`profiler` (sideways layer-2 edges) or on the
`scheduler` (which sits above it). Frontends, tools, and backends must never be
pulled in here. See `docs/architecture/03-dependency-graph.md`.

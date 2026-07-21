# `memory` module

The foundation allocator layer: a generic allocator interface (`hbi_allocator`),
built-in system and arena allocators, exact statistics, and a portable debug
mode. The tiered VRAM/RAM/NVMe placement, RAM-budget/OOM guard, and NUMA binding
described in PROJECT_CONTEXT §3.3 are built *on top of* this interface in a later
phase — they are intentionally **not** here yet.

## Status

`[BUILT]` — allocator interface + system/arena allocators + stats + debug.
No placement, tiering, or budget logic yet (later phase). No inference code.

## What it provides

- **`hbi_allocator`** — a vtable (`alloc`/`realloc`/`free` + `name`) plus an
  opaque context, so any subsystem takes an allocator without knowing its
  backing. Thin dispatchers `hbi_alloc`/`hbi_realloc`/`hbi_free` (DD-023).
- **Allocation tags** (`HBI_MEM_GENERAL/WEIGHTS/KV/SCRATCH`) — a coarse purpose
  label carried on each allocation for per-tag byte accounting and future
  placement (DD-009 groundwork). Does not change where memory comes from yet.
- **System allocator** (`hbi_allocator_system`) — process-wide, thread-safe,
  aligned (over the platform shim), with atomic statistics.
- **Arena allocator** (`hbi_arena_*`) — linear bump allocator over one backing
  block; `free` is a no-op, `reset` reclaims everything. Not thread-safe by
  design (one per worker) — ideal for per-forward scratch.
- **Statistics** (`hbi_mem_stats`) — live/peak bytes, alloc/free counts, live
  blocks, per-tag byte tallies (exact accounting in the spirit of Colibrì's
  `qt_bytes`, §3.3).
- **Debug mode** (`hbi_mem_debug_*`, `hbi_mem_check_leaks`) — red-zone canaries
  around each block validated on free, plus a live-block table for leak
  reporting. Portable (no OS tooling), so it runs everywhere including CI.

## Layout

| File | Role |
|------|------|
| `memory.h` | Allocator interface, allocators, stats, debug (`hbi_*`). |
| `memory_internal.h` | Private header — implementation details. |
| `memory.c` | System + arena allocators, statistics, debug checks. |
| `memory_test.c` | Unit tests (CTest target `memory`). |
| `CMakeLists.txt` | Build target `hb_memory`. |

## Allowed dependencies

`common`, `platform`. (Failures are reported through the `common` error record,
so no dependency on `logging` — the caller logs if it wishes.)

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. Never `backend`,
frontends, or tools. See `docs/architecture/03-dependency-graph.md`.

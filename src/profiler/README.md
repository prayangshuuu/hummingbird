# `profiler` module

Lightweight timing/counter/event instrumentation — the machine-readable
telemetry stream, deliberately separate from `logging` (human diagnostics).
Realizes PROJECT_CONTEXT principle 3, "measure, don't assume", and §3.14.

## Status

Implemented (Phase 5 runtime foundation). No inference logic — pure
instrumentation primitives.

## What it provides

- **Scopes** — named, nestable timing regions (`hbi_prof_scope_begin/end`,
  or the `HBI_PROF_SCOPE` cleanup-guard macro). Aggregated per name into
  call-count and total/min/max nanoseconds.
- **Counters** — named additive integer tallies (`hbi_prof_counter_add`).
- **Events** — named instantaneous marks with an optional value.
- **Readback** — `hbi_prof_stat_at` for raw records, `hbi_prof_report` for a
  plain-text report (no UI — that is a frontend concern).

## Cost model

Disabled by default. When disabled, every entry point is a single relaxed
atomic load plus a predictable branch — no clock read, no lookup, no
allocation. When enabled, a scope costs two monotonic clock reads and a hashed
name lookup. Name slots are reserved in a fixed table (`HBI_PROF_MAX_NAMES`), so
nothing allocates after warm-up; names beyond the cap are dropped and counted in
an overflow tally rather than growing a table on a hot path.

## Layout

| File | Role |
|------|------|
| `profiler.h` | Core-public header (`hbi_prof_*`), included by other modules. |
| `profiler_internal.h` | Private header — implementation details. |
| `profiler.c` | Implementation (atomic per-name slot table). |
| `profiler_test.c` | Unit tests (CTest target `profiler`). |
| `CMakeLists.txt` | Build target `hb_profiler`. |

## Allowed dependencies

`common`, `platform` (for the monotonic clock) only.

## Forbidden dependencies

`logging` (a sideways layer-2 edge) and anything higher. The profiler reports
misuse through the `common` error record, never by logging. Frontends, tools,
and backends must never be pulled in here. See
`docs/architecture/03-dependency-graph.md`.

# `scheduler` module

Overlaps I/O with compute and prefetches (PIPE/PILOT). Speculative actions never change output.

## Status

Scaffold only — no inference logic yet. Exposes `hbi_scheduler_name()` and `hbi_scheduler_selftest()` so the tree compiles and tests green.

## Layout

| File | Role |
|------|------|
| `scheduler.h` | Core-public header (`hbi_scheduler_*`), included by other modules. |
| `scheduler_internal.h` | Private header — implementation details, not for other modules. |
| `scheduler.c` | Implementation. |
| `scheduler_test.c` | Unit-test placeholder (CTest target `scheduler`). |
| `CMakeLists.txt` | Build target `hb_scheduler`. |

## Allowed dependencies

This module may depend **only** on: `common`, `memory`, `stream`, `backend`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. Frontends, tools, and backends-as-plugins must never be pulled in here. See `docs/architecture/03-dependency-graph.md` for the full rule set.

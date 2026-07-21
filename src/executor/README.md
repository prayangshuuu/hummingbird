# `executor` module

Walks a forward graph and dispatches each op node to its typed module on the active backend.

## Status

Scaffold only — no inference logic yet. Exposes `hbi_executor_name()` and `hbi_executor_selftest()` so the tree compiles and tests green.

## Layout

| File | Role |
|------|------|
| `executor.h` | Core-public header (`hbi_executor_*`), included by other modules. |
| `executor_internal.h` | Private header — implementation details, not for other modules. |
| `executor.c` | Implementation. |
| `executor_test.c` | Unit-test placeholder (CTest target `executor`). |
| `CMakeLists.txt` | Build target `hb_executor`. |

## Allowed dependencies

This module may depend **only** on: `common`, `graph`, `tensor`, `backend`, `kv`, `memory`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. Frontends, tools, and backends-as-plugins must never be pulled in here. See `docs/architecture/03-dependency-graph.md` for the full rule set.

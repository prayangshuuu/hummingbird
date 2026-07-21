# `graph` module

Forward-graph node types (RMSNORM/ATTENTION/MOE/MLP/RESIDUAL) and typed op-module registry.

## Status

Scaffold only — no inference logic yet. Exposes `hbi_graph_name()` and `hbi_graph_selftest()` so the tree compiles and tests green.

## Layout

| File | Role |
|------|------|
| `graph.h` | Core-public header (`hbi_graph_*`), included by other modules. |
| `graph_internal.h` | Private header — implementation details, not for other modules. |
| `graph.c` | Implementation. |
| `graph_test.c` | Unit-test placeholder (CTest target `graph`). |
| `CMakeLists.txt` | Build target `hb_graph`. |

## Allowed dependencies

This module may depend **only** on: `common`, `tensor`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. Frontends, tools, and backends-as-plugins must never be pulled in here. See `docs/architecture/03-dependency-graph.md` for the full rule set.

# `runtime` module

Orchestrator: owns the forward loop, sequences layers, drives the scheduler, produces logits.

## Status

Scaffold only — no inference logic yet. Exposes `hbi_runtime_name()` and `hbi_runtime_selftest()` so the tree compiles and tests green.

## Layout

| File | Role |
|------|------|
| `runtime.h` | Core-public header (`hbi_runtime_*`), included by other modules. |
| `runtime_internal.h` | Private header — implementation details, not for other modules. |
| `runtime.c` | Implementation. |
| `runtime_test.c` | Unit-test placeholder (CTest target `runtime`). |
| `CMakeLists.txt` | Build target `hb_runtime`. |

## Allowed dependencies

This module may depend **only** on: `common`, `config`, `logging`, `profiler`, `model`, `executor`, `scheduler`, `stream`, `context`, `memory`, `backend`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. Frontends, tools, and backends-as-plugins must never be pulled in here. See `docs/architecture/03-dependency-graph.md` for the full rule set.

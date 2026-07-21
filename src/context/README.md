# `context` module

Per-session decode context: KV state, sampling state, run mode.

## Status

Scaffold only — no inference logic yet. Exposes `hbi_context_name()` and `hbi_context_selftest()` so the tree compiles and tests green.

## Layout

| File | Role |
|------|------|
| `context.h` | Core-public header (`hbi_context_*`), included by other modules. |
| `context_internal.h` | Private header — implementation details, not for other modules. |
| `context.c` | Implementation. |
| `context_test.c` | Unit-test placeholder (CTest target `context`). |
| `CMakeLists.txt` | Build target `hb_context`. |

## Allowed dependencies

This module may depend **only** on: `common`, `memory`, `kv`, `model`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. Frontends, tools, and backends-as-plugins must never be pulled in here. See `docs/architecture/03-dependency-graph.md` for the full rule set.

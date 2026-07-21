# `backend` module

Stable extern-C backend ABI (CPU reference + optional CUDA/Metal) with per-tensor CPU fallback.

## Status

Scaffold only — no inference logic yet. Exposes `hbi_backend_name()` and `hbi_backend_selftest()` so the tree compiles and tests green.

## Layout

| File | Role |
|------|------|
| `backend.h` | Core-public header (`hbi_backend_*`), included by other modules. |
| `backend_internal.h` | Private header — implementation details, not for other modules. |
| `backend.c` | Implementation. |
| `backend_test.c` | Unit-test placeholder (CTest target `backend`). |
| `CMakeLists.txt` | Build target `hb_backend`. |

## Allowed dependencies

This module may depend **only** on: `common`, `device`, `tensor`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. Frontends, tools, and backends-as-plugins must never be pulled in here. See `docs/architecture/03-dependency-graph.md` for the full rule set.

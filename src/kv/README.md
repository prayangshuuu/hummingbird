# `kv` module

KV-cache abstraction: MHA/GQA and compressed-latent (MLA) layouts; save/restore.

## Status

Scaffold only — no inference logic yet. Exposes `hbi_kv_name()` and `hbi_kv_selftest()` so the tree compiles and tests green.

## Layout

| File | Role |
|------|------|
| `kv.h` | Core-public header (`hbi_kv_*`), included by other modules. |
| `kv_internal.h` | Private header — implementation details, not for other modules. |
| `kv.c` | Implementation. |
| `kv_test.c` | Unit-test placeholder (CTest target `kv`). |
| `CMakeLists.txt` | Build target `hb_kv`. |

## Allowed dependencies

This module may depend **only** on: `common`, `memory`, `tensor`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. Frontends, tools, and backends-as-plugins must never be pulled in here. See `docs/architecture/03-dependency-graph.md` for the full rule set.

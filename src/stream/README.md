# `stream` module

Weight streaming: contiguity to single coalesced read to zero-copy views (hard invariant).

## Status

Scaffold only — no inference logic yet. Exposes `hbi_stream_name()` and `hbi_stream_selftest()` so the tree compiles and tests green.

## Layout

| File | Role |
|------|------|
| `stream.h` | Core-public header (`hbi_stream_*`), included by other modules. |
| `stream_internal.h` | Private header — implementation details, not for other modules. |
| `stream.c` | Implementation. |
| `stream_test.c` | Unit-test placeholder (CTest target `stream`). |
| `CMakeLists.txt` | Build target `hb_stream`. |

## Allowed dependencies

This module may depend **only** on: `common`, `platform`, `memory`, `logging`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. Frontends, tools, and backends-as-plugins must never be pulled in here. See `docs/architecture/03-dependency-graph.md` for the full rule set.

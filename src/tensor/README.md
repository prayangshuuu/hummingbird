# `tensor` module

Quantized tensor representation and CPU matmul dispatch (exact + integer-dot regimes).

## Status

Scaffold only — no inference logic yet. Exposes `hbi_tensor_name()` and `hbi_tensor_selftest()` so the tree compiles and tests green.

## Layout

| File | Role |
|------|------|
| `tensor.h` | Core-public header (`hbi_tensor_*`), included by other modules. |
| `tensor_internal.h` | Private header — implementation details, not for other modules. |
| `tensor.c` | Implementation. |
| `tensor_test.c` | Unit-test placeholder (CTest target `tensor`). |
| `CMakeLists.txt` | Build target `hb_tensor`. |

## Allowed dependencies

This module may depend **only** on: `common`, `platform`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. Frontends, tools, and backends-as-plugins must never be pulled in here. See `docs/architecture/03-dependency-graph.md` for the full rule set.

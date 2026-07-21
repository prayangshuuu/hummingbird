# `quant` module

Quantization format registry: pack/unpack contracts and format detection.

## Status

Scaffold only — no inference logic yet. Exposes `hbi_quant_name()` and `hbi_quant_selftest()` so the tree compiles and tests green.

## Layout

| File | Role |
|------|------|
| `quant.h` | Core-public header (`hbi_quant_*`), included by other modules. |
| `quant_internal.h` | Private header — implementation details, not for other modules. |
| `quant.c` | Implementation. |
| `quant_test.c` | Unit-test placeholder (CTest target `quant`). |
| `CMakeLists.txt` | Build target `hb_quant`. |

## Allowed dependencies

This module may depend **only** on: `common`, `tensor`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. Frontends, tools, and backends-as-plugins must never be pulled in here. See `docs/architecture/03-dependency-graph.md` for the full rule set.

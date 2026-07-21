# `model` module

Model adapter: descriptor + forward graph + tokenizer ref + quant classes. New models are additive.

## Status

Scaffold only — no inference logic yet. Exposes `hbi_model_name()` and `hbi_model_selftest()` so the tree compiles and tests green.

## Layout

| File | Role |
|------|------|
| `model.h` | Core-public header (`hbi_model_*`), included by other modules. |
| `model_internal.h` | Private header — implementation details, not for other modules. |
| `model.c` | Implementation. |
| `model_test.c` | Unit-test placeholder (CTest target `model`). |
| `CMakeLists.txt` | Build target `hb_model`. |

## Allowed dependencies

This module may depend **only** on: `common`, `quant`, `graph`, `tokenizer`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. Frontends, tools, and backends-as-plugins must never be pulled in here. See `docs/architecture/03-dependency-graph.md` for the full rule set.

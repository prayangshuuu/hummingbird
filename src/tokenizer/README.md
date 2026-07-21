# `tokenizer` module

Tokenizer adapter interface (BPE/unigram/...); encode/decode.

## Status

Scaffold only — no inference logic yet. Exposes `hbi_tokenizer_name()` and `hbi_tokenizer_selftest()` so the tree compiles and tests green.

## Layout

| File | Role |
|------|------|
| `tokenizer.h` | Core-public header (`hbi_tokenizer_*`), included by other modules. |
| `tokenizer_internal.h` | Private header — implementation details, not for other modules. |
| `tokenizer.c` | Implementation. |
| `tokenizer_test.c` | Unit-test placeholder (CTest target `tokenizer`). |
| `CMakeLists.txt` | Build target `hb_tokenizer`. |

## Allowed dependencies

This module may depend **only** on: `common`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. Frontends, tools, and backends-as-plugins must never be pulled in here. See `docs/architecture/03-dependency-graph.md` for the full rule set.

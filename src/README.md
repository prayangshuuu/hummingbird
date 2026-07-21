# `src/` — the engine core

This is `libhummingbird`: a modular C17 inference engine (ADR DD-001, DD-002).
Each subdirectory is one module with a single responsibility, its own build
target (`hb_<module>`), and a strict, acyclic dependency contract. The aggregate
target `hummingbird` links them together behind the stable public ABI in
`include/hummingbird/`.

## Layout

Every module directory follows the same shape:

| File | Role |
|------|------|
| `<mod>.h` | **Core-public** header — the interface other modules may include. Symbols are `hbi_*` (internal, no external stability guarantee). |
| `<mod>_internal.h` | **Private** header — implementation details; no other module may include it. |
| `<mod>.c` | Implementation. |
| `<mod>_test.c` | Unit test (CTest target `<mod>`). |
| `CMakeLists.txt` | Builds `hb_<mod>` and its test. |
| `README.md` | Purpose, allowed/forbidden dependencies. |

## The modules (bottom-up)

Foundations first; each layer may depend only on layers below it. See
`docs/architecture/03-dependency-graph.md` for the authoritative rule set.

| Layer | Modules |
|-------|---------|
| 0 — foundation | `common` |
| 1 — platform/services | `platform`, then `logging`, `profiler`, `config` |
| 2 — compute primitives | `device`, `tensor`, `quant` |
| 3 — memory & data movement | `memory`, `stream`, `kv` |
| 4 — pluggable compute | `backend` |
| 5 — model description | `tokenizer`, `graph`, `model` |
| 6 — execution | `executor`, `scheduler` |
| 7 — session | `context` |
| 8 — orchestration | `runtime` |
| top — public bridge | `hummingbird.c` (implements the stable ABI) |

## Rules

- **No cycles.** A module includes only headers from modules strictly below it.
- **No god-structs.** State is passed via explicit context objects, not shared
  globals (a deliberate departure from Colibrì's `Model`).
- **`hbi_` prefix** on all core-public symbols; nothing internal leaks into
  `include/hummingbird/`.
- **`exit()` is banned** in the library — return an `hbi_status`. Only frontends
  may terminate the process (DD-011).

## Status

PHASE 4 bootstrap: every module is a compiling scaffold exposing
`hbi_<mod>_name()` and `hbi_<mod>_selftest()`. No inference logic exists yet — see
the roadmap in `.claude/PROJECT_CONTEXT.md` §6.

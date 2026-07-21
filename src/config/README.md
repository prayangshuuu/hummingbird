# `config` module

Typed, validated configuration: defaults < file < environment < programmatic
(DD-006, DD-024). Replaces Colibrì's ~80 ad-hoc `getenv()` calls with one
introspectable, bounds-checked store.

## Status

Implemented (Phase 5 runtime foundation). No inference logic — a foundation
subsystem. A config is a static **schema** (typed descriptors with defaults,
bounds, env names, help) plus a per-object value store that remembers which
**source** last set each entry.

## What it does

- Four value sources with strict precedence: `DEFAULT < FILE < ENV < SET`. A
  loader never overrides a value set by a higher-precedence source.
- Types: `bool`, `int64`, `uint64`, `string`. INT/UINT carry inclusive bounds.
- `key=value` file parsing (`#` comments, blank lines, whitespace trimmed).
- Environment loading for entries that declare an `HB_*` variable name.
- Single `key=value` overrides for CLI flags (`hbi_config_apply_kv`).
- Typed getters (with fallback + error record on misuse) and validating setters.
- Full introspection: enumerate entries, read each descriptor and its current
  source — enough for a `plan`/`doctor` command to dump the effective config.

Validation failures return an `hbi_status` and populate the thread-local error
record (naming the offending key/line); the caller decides whether to log.

## Layout

| File | Role |
|------|------|
| `config.h` | Public API (`hbi_config_*`): schema, lifecycle, load/get/set, introspection. |
| `config_internal.h` | Private header. |
| `config.c` | Implementation. |
| `config_test.c` | Unit tests (CTest target `config`): defaults, bounds, precedence, file load, introspection. |
| `CMakeLists.txt` | Build target `hb_config`. |

## Allowed dependencies

`common`, `platform` (for file I/O). **Not** `logging` — that would be a
forbidden sideways layer-2 edge; config reports via return codes + the error
record instead.

## Forbidden dependencies

Any other module, and any cyclic dependency. Frontends, tools, and backends
must never be pulled in here. See `docs/architecture/03-dependency-graph.md`.

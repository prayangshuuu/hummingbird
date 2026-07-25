# `json` — minimal JSON parser (layer 1)

A small, zero-dependency, bounds-checked recursive-descent JSON parser producing
an in-memory DOM. It exists to read the JSON that ships with models: the
**Safetensors file header**, `config.json`, and (later) `tokenizer.json`.

It is **not** a general-purpose high-performance JSON library — it is the
smallest correct thing that satisfies the loader's needs (RFC-015, DD-035).

## Scope

- Full RFC 8259 value grammar: objects, arrays, strings (with `\uXXXX` and
  surrogate pairs), numbers (via `strtod`), `true`/`false`/`null`.
- Hardening: explicit input length (no NUL required), max nesting depth
  (`HBI_JSON_MAX_DEPTH`), max input size (`HBI_JSON_MAX_BYTES`, 512 MiB).
- Every failure returns an `hbi_status` and sets the thread-local error record
  with a byte offset. Never aborts, never calls `exit()`.

## Ownership

Nodes and strings are allocated from a caller-provided `hbi_allocator`. Passing
an **arena** makes teardown a single `hbi_arena_reset`. Otherwise free the whole
DOM with `hbi_json_free` using the same allocator.

## Dependencies

- **Allowed:** `common`, `memory`.
- **Forbidden:** everything else. `json` is a leaf utility; nothing model- or
  tensor-specific belongs here.

## Files

- `json.h` — public API (kinds, value struct, parse/free, accessors).
- `json_internal.h` — parser cursor struct (private to `json.c`).
- `json.c` — the parser + accessors.
- `json_test.c` — unit tests (scalars, numbers, objects, arrays, escapes,
  malformed inputs).

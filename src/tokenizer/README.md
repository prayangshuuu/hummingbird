# `tokenizer` module

Tokenizer Framework (RFC-013): model-independent text↔token conversion with a
pluggable vtable interface, vocabulary abstraction, and encoding/decoding
pipelines.

## Status

Framework implemented with a mock tokenizer for testing. No real tokenizer
implementations (BPE, SentencePiece, WordPiece, Unigram) — those are separate
deliverables.

## Layout

| File | Role |
|------|------|
| `tokenizer.h` | Core-public header: UTF-8 utilities, token sequence, vocabulary, decode state, tokenizer vtable, registry, manager, statistics. |
| `tokenizer_internal.h` | Private header — concrete structs (vocab hash table, sequence, decode state, manager), mock registration helpers. |
| `tokenizer.c` | Implementation: UTF-8 encode/decode/validate, token sequence, vocabulary (FNV-1a hash table), registry, manager, statistics. |
| `tokenizer_mock.c` | Mock byte-level tokenizer for testing (each byte → token ID). |
| `tokenizer_test.c` | Comprehensive unit tests (CTest target `tokenizer`). |
| `CMakeLists.txt` | Build target `hb_tokenizer`. |

## Allowed dependencies

This module may depend **only** on: `common`, `memory`, `platform`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. In particular: `executor`,
`scheduler`, `backend`, `model`, `adapter`, frontends, and tools must never be
pulled in here. See `docs/architecture/03-dependency-graph.md` for the full
rule set.

## How to implement a new tokenizer

1. Create a file (e.g. `tokenizer_bpe.c`) implementing every callback in the
   `hbi_tokenizer` vtable.
2. Provide a static `const hbi_tokenizer` instance with your callbacks.
3. Register it at init time via `hbi_tokenizer_register()`.
4. Your `init` should set up any merge tables, trie structures, etc.
5. Your `encode` runs the full pipeline: normalize → pretokenize → tokenize → post-process.
6. Your `decode` converts token IDs back to UTF-8 text.
7. Optionally implement `encode_incremental` and `decode_incremental` for streaming.
8. Implement `free_context` and `shutdown` for cleanup.

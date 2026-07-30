RFC-017: Tokenizer Integration — Encoding, Decoding, and Vocabulary Management

Status: DRAFT — Implementation Milestone: M3
Authors: Hummingbird Core Team

---

## 1. Motivation

The Hummingbird runtime accepts raw token IDs as input to `hbi_runtime_generate()`
(RFC-013). In practice, users provide text strings and expect text output.
A tokenizer is needed to bridge the gap between human-readable text and the
model's vocabulary.

RFC-017 specifies the `tokenizer` module, which provides:
- Text → token-ID sequence encoding
- Token-ID → text decoding (streaming-capable)
- Vocabulary loading from model metadata
- Support for the tokenization schemes used by LLaMA, GPT-2, Mistral, and
  Phi model families

---

## 2. Tokenizer Formats

All target model families use Byte Pair Encoding (BPE) or its variants.
The tokenizer configuration is embedded in the model file:

| Model format | Tokenizer location |
|---|---|
| GGUF | `tokenizer.*` metadata keys |
| Safetensors | Separate `tokenizer.json` sidecar (HuggingFace format) |

RFC-017 supports both sources. The tokenizer module detects which format
is in use via the model metadata returned by the loader (RFC-011).

---

## 3. Public Internal API

```c
/* src/tokenizer/tokenizer.h — new module (layer 8, same layer as adapter) */

typedef struct hbi_tokenizer hbi_tokenizer;

/* Load the tokenizer vocabulary from the model session's metadata.
 * Supports GGUF-embedded and HuggingFace-sidecar tokenizers.
 * Returns HBI_ERR_NOT_FOUND if no tokenizer data is present in the session.
 * Returns HBI_ERR_UNSUPPORTED for unknown tokenizer types. */
hbi_status hbi_tokenizer_create(const hbi_load_session *session,
                                hbi_allocator          *allocator,
                                hbi_tokenizer         **out);

/* Destroy a tokenizer. NULL-safe. */
void hbi_tokenizer_destroy(hbi_tokenizer *tok);

/* Encode `text` into token IDs.
 * `out_ids` is caller-allocated with capacity `capacity`.
 * `out_count` receives the actual number of tokens.
 * Returns HBI_ERR_INVALID_ARG if out is too small (caller should retry
 * with a larger buffer). */
hbi_status hbi_tokenizer_encode(const hbi_tokenizer *tok,
                                const char          *text,
                                uint32_t            *out_ids,
                                size_t               capacity,
                                size_t              *out_count);

/* Decode a single token ID into its UTF-8 byte sequence.
 * `buf` must have at least `HBI_TOKENIZER_MAX_TOKEN_BYTES` capacity.
 * `out_len` receives the byte count (may be 0 for special tokens). */
#define HBI_TOKENIZER_MAX_TOKEN_BYTES 256u

hbi_status hbi_tokenizer_decode_token(const hbi_tokenizer *tok,
                                      uint32_t             token_id,
                                      char                *buf,
                                      size_t              *out_len);

/* Special token ID accessors. Return UINT32_MAX if not defined by this vocab. */
uint32_t hbi_tokenizer_bos_id(const hbi_tokenizer *tok);
uint32_t hbi_tokenizer_eos_id(const hbi_tokenizer *tok);
uint32_t hbi_tokenizer_pad_id(const hbi_tokenizer *tok);

/* Vocabulary size. */
uint32_t hbi_tokenizer_vocab_size(const hbi_tokenizer *tok);

/* Module identity. */
const char  *hbi_tokenizer_name(void);
hbi_status   hbi_tokenizer_selftest(void);
```

---

## 4. Implementation Plan (M3)

### Phase A: BPE core

1. Implement the BPE merge algorithm:
   - Vocabulary table (token bytes → ID, ID → token bytes)
   - Merge rule table (sorted by BPE merge priority)
   - Encode: UTF-8 → byte sequence → BPE merge loop
   - Decode: ID → UTF-8 with SentencePiece-style space normalization

2. Unit tests with fixed vocabulary (does not depend on model files):
   - Round-trip: encode → decode → original text
   - Edge cases: empty string, Unicode CJK, emoji, null bytes
   - Vocab boundary: token ID = vocab_size - 1 (last valid)
   - Out-of-range token ID returns HBI_ERR_NOT_FOUND

### Phase B: GGUF tokenizer loading

3. Parse `tokenizer.gguf_pre` metadata key
4. Extract `tokenizer.ggf.model` (BPE merge table) and
   `tokenizer.gguf.tokens` (byte vocabulary)
5. Integration test: load a tiny GGUF file with embedded tokenizer,
   encode a known prompt, verify token IDs match a reference

### Phase C: HuggingFace sidecar loading

6. Parse `tokenizer.json` (JSON format) from the model directory
7. Support `type: BPE` and `type: WordPiece` (Mistral/Phi use BPE)
8. Integration test: load `tokenizer.json`, encode a known prompt

---

## 5. Thread Safety

An `hbi_tokenizer` is read-only after creation. Concurrent encoding and
decoding from multiple threads is safe. The creation function is not
thread-safe; create the tokenizer before spawning workers.

---

## 6. Dependencies

| Module | Reason |
|---|---|
| `model` | Provides the load session (metadata and file path) |
| `memory` | Allocator for vocabulary tables |
| `platform` | File I/O (for sidecar tokenizer.json) |
| `common` | Error records, status codes |

The `tokenizer` module MUST NOT depend on `adapter`, `kv`, or `runtime`
(it is on the same layer as `adapter` — layer 8 — and lateral edges are
forbidden).

---

## 7. Known Limitations (M3 Scope)

- BPE only. WordPiece and Unigram tokenizers are out of scope for M3.
- Detokenizer does not handle multi-byte token stream reconstruction
  (the caller must buffer and detect UTF-8 boundaries).
- No batch encoding (tokenize multiple sequences simultaneously).
- Chat template formatting (system/user/assistant roles) is out of scope;
  callers format the prompt before calling `hbi_tokenizer_encode()`.

---

## 8. Open Questions

1. Should `hbi_tokenizer_encode()` automatically prepend BOS?
   Proposal: No. The caller is responsible. Pass `hbi_tokenizer_bos_id()`
   explicitly if needed.

2. Should the tokenizer be part of the `hbi_load_session` or separate?
   Proposal: Separate. The model session owns weight metadata; the tokenizer
   is vocabulary metadata. Keeping them separate allows reusing a tokenizer
   across multiple model files (e.g., different quantizations of the same model).

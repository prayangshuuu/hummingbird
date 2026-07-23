/* tokenizer.h — Tokenizer Framework: model-independent text↔token conversion,
 * vocabulary abstraction, encoding/decoding pipelines, and plugin registry
 * (RFC-013).
 *
 * Core-public header for the `tokenizer` module (layer 4). Other modules include
 * this; external embedders use <hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee).
 *
 * ── Design (RFC-013) ─────────────────────────────────────────────────────────
 * The Tokenizer Framework converts text into model tokens and tokens back into
 * text. The framework is model-independent, format-independent, and supports
 * streaming tokenization and incremental decoding.
 *
 * Tokenizer lifecycle:
 *   1. Register tokenizer implementations at init time (before inference)
 *   2. Create a vocabulary from model metadata or a tokenizer file
 *   3. Create a tokenizer manager (vocabulary + implementation)
 *   4. Encode text → token sequence
 *   5. Decode token sequence → text
 *   6. Use incremental encode/decode for streaming
 *   7. Destroy manager when done
 *
 * Encoding pipeline (conceptual stages):
 *   normalize → pretokenize → tokenize → post-process
 * Each stage is implemented within the tokenizer's encode callback; the
 * framework provides the vocabulary and sequence abstractions.
 *
 * ── Ownership ────────────────────────────────────────────────────────────────
 * Vocabulary is owned by the caller; the manager borrows it. Token sequences
 * are owned by the caller. The tokenizer vtable is a static const. The manager
 * owns the encode context (for incremental encoding).
 *
 * ── Thread-safety ────────────────────────────────────────────────────────────
 * The tokenizer registry is populated at init time (before workers), like other
 * registries. A tokenizer manager is not thread-safe; serialize externally or
 * use one per inference session.
 */
#ifndef HB_TOKENIZER_H
#define HB_TOKENIZER_H

#include "common/common.h"
#include "memory/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ─────────────────────────────────────────────────────────────── */

#define HBI_TOKENIZER_NAME_MAX 64u    /* max tokenizer name length */
#define HBI_TOKEN_TEXT_MAX 256u       /* max single token text length */
#define HBI_TOKEN_SEQ_INITIAL_CAP 64u /* initial token sequence capacity */

/* Invalid token ID sentinel. */
#define HBI_TOKEN_INVALID_ID UINT32_MAX

/* ── Token ID ──────────────────────────────────────────────────────────────── */

typedef uint32_t hbi_token_id;

/* ── UTF-8 utilities ─────────────────────────────────────────────────────────
 * Stateless helpers for working with UTF-8 encoded text. These are used by
 * tokenizer implementations and by the framework's validation routines. */

/* Byte length of a UTF-8 sequence from its leading byte. Returns 1-4 for valid
 * leading bytes, 0 for invalid (continuation bytes, 0xFE, 0xFF). */
uint32_t hbi_utf8_seq_len(uint8_t leading_byte);

/* Decode one codepoint from `src` (up to `src_len` bytes). On success, stores
 * the codepoint in *out_cp and returns the number of bytes consumed (1-4).
 * Returns 0 on invalid UTF-8 (and *out_cp = U+FFFD). */
uint32_t hbi_utf8_decode(const uint8_t *src, size_t src_len, uint32_t *out_cp);

/* Encode one codepoint into `dst` (must have room for 4 bytes). Returns the
 * number of bytes written (1-4), or 0 if the codepoint is invalid (> 0x10FFFF
 * or surrogate). */
uint32_t hbi_utf8_encode(uint32_t codepoint, uint8_t *dst);

/* Validate that `text` (length `len`) is well-formed UTF-8. Returns true if
 * valid, false on the first invalid byte sequence. */
bool hbi_utf8_validate(const uint8_t *text, size_t len);

/* ── Special token types ───────────────────────────────────────────────────── */

typedef enum hbi_special_token_type {
    HBI_SPECIAL_UNKNOWN = 0, /* unknown/unrecognized token */
    HBI_SPECIAL_PAD,         /* padding token */
    HBI_SPECIAL_BOS,         /* beginning of sequence */
    HBI_SPECIAL_EOS,         /* end of sequence */
    HBI_SPECIAL_SEP,         /* separator */
    HBI_SPECIAL_CLS,         /* classification token */
    HBI_SPECIAL_MASK,        /* mask token */
    HBI_SPECIAL_COUNT        /* sentinel */
} hbi_special_token_type;

const char *hbi_special_token_type_str(hbi_special_token_type t);

/* ── Token sequence ──────────────────────────────────────────────────────────
 * A growable array of token IDs produced by encoding. The caller owns the
 * sequence and must destroy it when done. */

typedef struct hbi_token_sequence hbi_token_sequence;

hbi_status hbi_token_sequence_create(hbi_token_sequence **out, hbi_allocator *allocator);
void hbi_token_sequence_destroy(hbi_token_sequence *seq); /* NULL-safe */

/* Append a token ID. Grows the backing array as needed. */
hbi_status hbi_token_sequence_append(hbi_token_sequence *seq, hbi_token_id id);

/* Append multiple token IDs at once. */
hbi_status hbi_token_sequence_append_many(hbi_token_sequence *seq, const hbi_token_id *ids,
                                          size_t count);

/* Accessors. */
uint32_t hbi_token_sequence_count(const hbi_token_sequence *seq);
hbi_token_id hbi_token_sequence_get(const hbi_token_sequence *seq, uint32_t index);
const hbi_token_id *hbi_token_sequence_data(const hbi_token_sequence *seq);

/* Reset the sequence to zero tokens (does not free capacity). */
void hbi_token_sequence_clear(hbi_token_sequence *seq);

/* ── Vocabulary ──────────────────────────────────────────────────────────────
 * A bidirectional mapping between token text and token IDs. Supports special
 * tokens, unknown tokens, and reserved IDs. The vocabulary is independent of
 * any specific tokenizer implementation — BPE, SentencePiece, WordPiece, and
 * Unigram all use the same vocabulary structure. */

typedef struct hbi_vocabulary hbi_vocabulary;

hbi_status hbi_vocabulary_create(hbi_vocabulary **out, hbi_allocator *allocator,
                                 size_t initial_capacity);
void hbi_vocabulary_destroy(hbi_vocabulary *vocab); /* NULL-safe */

/* Add a token with the given ID. Text is copied. Fails HBI_ERR_STATE if the ID
 * is already in use with different text, or HBI_ERR_INVALID_ARG on bad input. */
hbi_status hbi_vocabulary_add(hbi_vocabulary *vocab, const char *text, hbi_token_id id);

/* Mark an existing token ID as a special token of the given type.
 * Fails HBI_ERR_NOT_FOUND if the ID is not in the vocabulary. */
hbi_status hbi_vocabulary_set_special(hbi_vocabulary *vocab, hbi_token_id id,
                                      hbi_special_token_type type);

/* Look up a token by text. Returns HBI_TOKEN_INVALID_ID if not found. */
hbi_token_id hbi_vocabulary_lookup(const hbi_vocabulary *vocab, const char *text);

/* Look up a token's text by ID. Returns NULL if not found. */
const char *hbi_vocabulary_text(const hbi_vocabulary *vocab, hbi_token_id id);

/* Number of tokens in the vocabulary. */
uint32_t hbi_vocabulary_size(const hbi_vocabulary *vocab);

/* Query whether a token ID is a special token. */
bool hbi_vocabulary_is_special(const hbi_vocabulary *vocab, hbi_token_id id);

/* Get the special token ID for a given type. Returns HBI_TOKEN_INVALID_ID if
 * not configured. */
hbi_token_id hbi_vocabulary_special_id(const hbi_vocabulary *vocab, hbi_special_token_type type);

/* Validate the vocabulary: check for duplicate texts, missing required special
 * tokens (unknown), and well-formed token text (valid UTF-8). */
hbi_status hbi_vocabulary_validate(const hbi_vocabulary *vocab);

/* ── Decode state ────────────────────────────────────────────────────────────
 * Tracks partial output across incremental decode calls. Handles incomplete
 * UTF-8 sequences that span decode boundaries. The caller creates one state per
 * streaming decode session. */

typedef struct hbi_decode_state hbi_decode_state;

hbi_status hbi_decode_state_create(hbi_decode_state **out, hbi_allocator *allocator);
void hbi_decode_state_destroy(hbi_decode_state *state); /* NULL-safe */
void hbi_decode_state_reset(hbi_decode_state *state);

/* ── Tokenizer vtable ────────────────────────────────────────────────────────
 * Each tokenizer implementation (BPE, SentencePiece, WordPiece, Unigram, etc.)
 * provides one static instance of this struct. The runtime interacts ONLY
 * through these function pointers. */

typedef struct hbi_tokenizer hbi_tokenizer;

struct hbi_tokenizer {
    const char *name; /* e.g. "bpe", "sentencepiece", "mock" */

    /* Initialize tokenizer-private state from a vocabulary. Called once after
     * the vocabulary is populated. May allocate merge tables, trie structures,
     * etc. */
    hbi_status (*init)(const hbi_tokenizer *self, const hbi_vocabulary *vocab,
                       hbi_allocator *allocator);

    /* Encode text into a token sequence. The implementation runs the full
     * pipeline: normalize → pretokenize → tokenize → post-process. The
     * sequence is appended to (not cleared), so callers may pre-populate with
     * BOS or other prefix tokens. */
    hbi_status (*encode)(const hbi_tokenizer *self, const hbi_vocabulary *vocab, const char *text,
                         size_t text_len, hbi_token_sequence *out_seq);

    /* Decode a token sequence into text. Writes UTF-8 bytes into `out_text`
     * (up to `out_capacity`). *out_len receives the number of bytes written
     * (excluding NUL). The output is always NUL-terminated when out_capacity > 0. */
    hbi_status (*decode)(const hbi_tokenizer *self, const hbi_vocabulary *vocab,
                         const hbi_token_id *tokens, uint32_t token_count, char *out_text,
                         size_t out_capacity, size_t *out_len);

    /* Incremental encode: tokenize `text` using `context` (an opaque pointer
     * the tokenizer allocates on first call) to maintain state across calls.
     * May be NULL if the implementation does not support streaming encode. */
    hbi_status (*encode_incremental)(const hbi_tokenizer *self, const hbi_vocabulary *vocab,
                                     const char *text, size_t text_len, hbi_token_sequence *out_seq,
                                     void **context);

    /* Incremental decode: decode one token at a time, using `state` to track
     * partial UTF-8 sequences. Writes decoded bytes into out_text.
     * May be NULL if the implementation does not support streaming decode. */
    hbi_status (*decode_incremental)(const hbi_tokenizer *self, const hbi_vocabulary *vocab,
                                     hbi_token_id token, hbi_decode_state *state, char *out_text,
                                     size_t out_capacity, size_t *out_len);

    /* Free a context allocated by encode_incremental. NULL-safe. */
    void (*free_context)(const hbi_tokenizer *self, void *context);

    /* Tear down tokenizer-private global state (if any). NULL-safe. */
    void (*shutdown)(const hbi_tokenizer *self);
};

/* ── Tokenizer registry ──────────────────────────────────────────────────────
 * A fixed-capacity, name-keyed registry populated at init time (before
 * workers), like the adapter and format-handler registries. */

hbi_status hbi_tokenizer_register(const hbi_tokenizer *tokenizer);

/* Find a tokenizer by name. Returns NULL if not found. */
const hbi_tokenizer *hbi_tokenizer_find(const char *name);

/* Number of registered tokenizers. */
int hbi_tokenizer_count(void);

/* Clear all registered tokenizers. For test isolation. */
void hbi_tokenizer_registry_clear(void);

/* ── Tokenizer manager ───────────────────────────────────────────────────────
 * Binds a tokenizer implementation with a vocabulary and provides the primary
 * encode/decode interface. The manager borrows the vocabulary (does not own it)
 * and the tokenizer (registry-owned). */

typedef struct hbi_tokenizer_manager hbi_tokenizer_manager;

hbi_status hbi_tokenizer_manager_create(hbi_tokenizer_manager **out, const hbi_tokenizer *tokenizer,
                                        const hbi_vocabulary *vocab, hbi_allocator *allocator);

void hbi_tokenizer_manager_destroy(hbi_tokenizer_manager *manager); /* NULL-safe */

/* Full encode: text → token sequence. */
hbi_status hbi_tokenizer_manager_encode(const hbi_tokenizer_manager *manager, const char *text,
                                        size_t text_len, hbi_token_sequence *out_seq);

/* Full decode: token sequence → text. */
hbi_status hbi_tokenizer_manager_decode(const hbi_tokenizer_manager *manager,
                                        const hbi_token_id *tokens, uint32_t token_count,
                                        char *out_text, size_t out_capacity, size_t *out_len);

/* Incremental encode (streaming). */
hbi_status hbi_tokenizer_manager_encode_incremental(const hbi_tokenizer_manager *manager,
                                                    const char *text, size_t text_len,
                                                    hbi_token_sequence *out_seq);

/* Incremental decode (one token at a time). */
hbi_status hbi_tokenizer_manager_decode_incremental(const hbi_tokenizer_manager *manager,
                                                    hbi_token_id token, char *out_text,
                                                    size_t out_capacity, size_t *out_len);

/* Reset the manager's streaming state (encode context + decode state). */
void hbi_tokenizer_manager_reset(hbi_tokenizer_manager *manager);

/* Accessors. */
const hbi_vocabulary *hbi_tokenizer_manager_vocabulary(const hbi_tokenizer_manager *manager);
const hbi_tokenizer *hbi_tokenizer_manager_tokenizer(const hbi_tokenizer_manager *manager);

/* ── Tokenizer statistics ────────────────────────────────────────────────────
 * Per-manager counters for observability. */

typedef struct hbi_tokenizer_statistics {
    uint64_t encode_time_ns;     /* cumulative encode wall time */
    uint64_t decode_time_ns;     /* cumulative decode wall time */
    uint64_t tokens_encoded;     /* total tokens produced by encode */
    uint64_t tokens_decoded;     /* total tokens consumed by decode */
    uint64_t vocabulary_lookups; /* vocabulary lookup calls */
    uint64_t encode_calls;       /* number of encode invocations */
    uint64_t decode_calls;       /* number of decode invocations */
} hbi_tokenizer_statistics;

hbi_status hbi_tokenizer_manager_get_statistics(const hbi_tokenizer_manager *manager,
                                                hbi_tokenizer_statistics *out);

/* ── Module identity / self-test ──────────────────────────────────────────── */

const char *hbi_tokenizer_name(void);
hbi_status hbi_tokenizer_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_TOKENIZER_H */

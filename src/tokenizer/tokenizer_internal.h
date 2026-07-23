/* tokenizer_internal.h — private to the `tokenizer` module (RFC-013).
 *
 * Nothing here is visible to other modules. Concrete struct definitions for
 * the vocabulary (hash table + reverse map), token sequence (growable array),
 * decode state (UTF-8 streaming buffer), and tokenizer manager (vtable + vocab
 * + statistics). Also declares mock-tokenizer registration helpers.
 */
#ifndef HB_TOKENIZER_INTERNAL_H
#define HB_TOKENIZER_INTERNAL_H

#include "platform/platform.h"
#include "tokenizer/tokenizer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── Registry capacity ────────────────────────────────────────────────────── */

#define HBI_TOKENIZER_REGISTRY_MAX 16u

/* ── Token sequence ───────────────────────────────────────────────────────── */

struct hbi_token_sequence {
    hbi_token_id *data;
    uint32_t count;
    uint32_t capacity;
    hbi_allocator *allocator;
};

/* ── Vocabulary entry ─────────────────────────────────────────────────────── */

typedef struct hbi_vocab_entry {
    char text[HBI_TOKEN_TEXT_MAX]; /* token text (UTF-8, NUL-terminated) */
    hbi_token_id id;               /* token ID */
    bool occupied;                 /* hash-table slot is in use */
    bool is_special;               /* this token is a special token */
    hbi_special_token_type special_type;
} hbi_vocab_entry;

/* ── Vocabulary ───────────────────────────────────────────────────────────── */

struct hbi_vocabulary {
    hbi_vocab_entry *table;                      /* open-addressing hash table */
    size_t table_capacity;                       /* number of slots (power of 2) */
    uint32_t count;                              /* number of entries */
    hbi_token_id special_ids[HBI_SPECIAL_COUNT]; /* special token ID per type */
    hbi_allocator *allocator;
};

/* ── Decode state ─────────────────────────────────────────────────────────── */

struct hbi_decode_state {
    uint8_t pending[4];   /* partial UTF-8 bytes from previous call */
    uint32_t pending_len; /* 0-3 bytes buffered */
    hbi_allocator *allocator;
};

/* ── Tokenizer manager ────────────────────────────────────────────────────── */

struct hbi_tokenizer_manager {
    const hbi_tokenizer *tokenizer;   /* borrowed from registry */
    const hbi_vocabulary *vocabulary; /* borrowed from caller */
    hbi_allocator *allocator;         /* borrowed */
    void *encode_context;             /* incremental encode state (tokenizer-owned) */
    hbi_decode_state *decode_state;   /* incremental decode state */
    hbi_tokenizer_statistics stats;   /* cumulative statistics */
    bool initialized;                 /* lifecycle guard */
};

/* ── Mock tokenizer registration ──────────────────────────────────────────── */

hbi_status hbi_tokenizer_mock_register(void);
const hbi_tokenizer *hbi_tokenizer_mock_get(void);

#endif /* HB_TOKENIZER_INTERNAL_H */

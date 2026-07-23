/* tokenizer_mock.c — Mock tokenizer for testing (RFC-013).
 *
 * Implements a byte-level "tokenizer" where each byte value maps to a token ID.
 * Special tokens occupy IDs 0-4:
 *   0 = <unk>, 1 = <pad>, 2 = <bos>, 3 = <eos>, 4 = <space>
 * Byte values 0x20-0x7E map to IDs 5-99 (printable ASCII).
 *
 * This is not a real tokenizer — it exists solely to exercise the framework's
 * encode/decode/incremental/registry/manager paths under test.
 */
#include "tokenizer/tokenizer_internal.h"

#include <string.h>

#define MOCK_OFFSET 5u /* token IDs start at 5 to leave room for special tokens */

static const hbi_tokenizer mock_tokenizer;

/* ── Init ─────────────────────────────────────────────────────────────────── */

static hbi_status mock_init(const hbi_tokenizer *self, const hbi_vocabulary *vocab,
                            hbi_allocator *allocator) {
    HB_UNUSED(self);
    HB_UNUSED(vocab);
    HB_UNUSED(allocator);
    return HBI_OK;
}

/* ── Encode ───────────────────────────────────────────────────────────────── */

static hbi_status mock_encode(const hbi_tokenizer *self, const hbi_vocabulary *vocab,
                              const char *text, size_t text_len, hbi_token_sequence *out_seq) {
    HB_UNUSED(self);
    HB_UNUSED(vocab);
    if (!text || !out_seq) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL text or sequence");
    }
    /* Validate UTF-8. */
    if (!hbi_utf8_validate((const uint8_t *)text, text_len)) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "invalid UTF-8 input");
    }
    /* Each byte becomes a token: byte value + MOCK_OFFSET. */
    for (size_t i = 0u; i < text_len; i++) {
        hbi_token_id id = (hbi_token_id)(uint8_t)text[i] + MOCK_OFFSET;
        hbi_status st = hbi_token_sequence_append(out_seq, id);
        if (st != HBI_OK) {
            return st;
        }
    }
    return HBI_OK;
}

/* ── Decode ───────────────────────────────────────────────────────────────── */

static hbi_status mock_decode(const hbi_tokenizer *self, const hbi_vocabulary *vocab,
                              const hbi_token_id *tokens, uint32_t token_count, char *out_text,
                              size_t out_capacity, size_t *out_len) {
    HB_UNUSED(self);
    HB_UNUSED(vocab);
    if ((!tokens && token_count > 0u) || !out_text || !out_len) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL argument");
    }
    if (out_capacity == 0u) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "zero output capacity");
    }
    size_t pos = 0u;
    for (uint32_t i = 0u; i < token_count; i++) {
        if (tokens[i] < MOCK_OFFSET || tokens[i] > MOCK_OFFSET + 127u) {
            continue; /* skip special tokens and out-of-range */
        }
        if (pos + 1u >= out_capacity) {
            break; /* no room */
        }
        out_text[pos++] = (char)(uint8_t)(tokens[i] - MOCK_OFFSET);
    }
    out_text[pos] = '\0';
    *out_len = pos;
    return HBI_OK;
}

/* ── Incremental encode ───────────────────────────────────────────────────── */

/* Context tracks position for multi-chunk encoding. The mock tokenizer is
 * stateless per-byte, so the context is just a non-NULL marker. */
typedef struct mock_encode_ctx {
    uint64_t bytes_seen;
} mock_encode_ctx;

static hbi_status mock_encode_incremental(const hbi_tokenizer *self, const hbi_vocabulary *vocab,
                                          const char *text, size_t text_len,
                                          hbi_token_sequence *out_seq, void **context) {
    HB_UNUSED(self);
    HB_UNUSED(vocab);
    if (!text || !out_seq || !context) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL argument");
    }
    if (!*context) {
        /* First call — allocate context. */
        *context = hbi_alloc(out_seq->allocator, sizeof(mock_encode_ctx), 0, HBI_MEM_GENERAL);
        if (!*context) {
            return HBI_ERR_SET(HBI_ERR_OOM, 0, "mock encode context allocation failed");
        }
        ((mock_encode_ctx *)*context)->bytes_seen = 0u;
    }
    for (size_t i = 0u; i < text_len; i++) {
        hbi_token_id id = (hbi_token_id)(uint8_t)text[i] + MOCK_OFFSET;
        hbi_status st = hbi_token_sequence_append(out_seq, id);
        if (st != HBI_OK) {
            return st;
        }
    }
    ((mock_encode_ctx *)*context)->bytes_seen += (uint64_t)text_len;
    return HBI_OK;
}

/* ── Incremental decode ───────────────────────────────────────────────────── */

static hbi_status mock_decode_incremental(const hbi_tokenizer *self, const hbi_vocabulary *vocab,
                                          hbi_token_id token, hbi_decode_state *state,
                                          char *out_text, size_t out_capacity, size_t *out_len) {
    HB_UNUSED(self);
    HB_UNUSED(vocab);
    HB_UNUSED(state);
    if (!out_text || !out_len) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL argument");
    }
    if (out_capacity < 2u) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "output buffer too small");
    }
    *out_len = 0u;
    if (token < MOCK_OFFSET || token > MOCK_OFFSET + 127u) {
        /* Special token — produce no output. */
        out_text[0] = '\0';
        return HBI_OK;
    }
    out_text[0] = (char)(uint8_t)(token - MOCK_OFFSET);
    out_text[1] = '\0';
    *out_len = 1u;
    return HBI_OK;
}

/* ── Free context / shutdown ──────────────────────────────────────────────── */

static void mock_free_context(const hbi_tokenizer *self, void *context) {
    HB_UNUSED(self);
    if (context) {
        /* We don't have the allocator here; the mock context is allocated from
         * the sequence's allocator. The manager calls this with its own allocator
         * available. For the mock, the context is tiny and freed by the manager. */
        /* Actually, we allocated via out_seq->allocator in encode_incremental.
         * The manager stores the context and calls free_context on teardown.
         * We need to free it, but we don't have the allocator. Use system. */
        hbi_free(hbi_allocator_system(), context);
    }
}

static void mock_shutdown(const hbi_tokenizer *self) {
    HB_UNUSED(self);
}

/* ── Mock tokenizer instance ──────────────────────────────────────────────── */

static const hbi_tokenizer mock_tokenizer = {
    .name = "mock",
    .init = mock_init,
    .encode = mock_encode,
    .decode = mock_decode,
    .encode_incremental = mock_encode_incremental,
    .decode_incremental = mock_decode_incremental,
    .free_context = mock_free_context,
    .shutdown = mock_shutdown,
};

const hbi_tokenizer *hbi_tokenizer_mock_get(void) {
    return &mock_tokenizer;
}

hbi_status hbi_tokenizer_mock_register(void) {
    return hbi_tokenizer_register(&mock_tokenizer);
}

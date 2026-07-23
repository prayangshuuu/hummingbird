/* tokenizer_test.c — comprehensive tests for the Tokenizer Framework (RFC-013). */
#include "tokenizer/tokenizer_internal.h"

#include "hbi_test.h"

#include <stdio.h>
#include <string.h>

/* ── Helper ───────────────────────────────────────────────────────────────── */

static hbi_allocator *test_alloc(void) {
    return hbi_allocator_system();
}

/* Build a small test vocabulary:
 *   ID 0 = <unk> (special: UNKNOWN)
 *   ID 1 = <pad> (special: PAD)
 *   ID 2 = <bos> (special: BOS)
 *   ID 3 = <eos> (special: EOS)
 *   ID 5-132 = bytes 0x00-0x7F + MOCK_OFFSET(5)
 * Plus a few word tokens for vocabulary tests. */
static hbi_status setup_test_vocab(hbi_vocabulary **out) {
    hbi_allocator *a = test_alloc();
    hbi_status st = hbi_vocabulary_create(out, a, 256u);
    if (st != HBI_OK) {
        return st;
    }
    /* Special tokens. */
    st = hbi_vocabulary_add(*out, "<unk>", 0u);
    if (st != HBI_OK) {
        return st;
    }
    st = hbi_vocabulary_set_special(*out, 0u, HBI_SPECIAL_UNKNOWN);
    if (st != HBI_OK) {
        return st;
    }
    st = hbi_vocabulary_add(*out, "<pad>", 1u);
    if (st != HBI_OK) {
        return st;
    }
    st = hbi_vocabulary_set_special(*out, 1u, HBI_SPECIAL_PAD);
    if (st != HBI_OK) {
        return st;
    }
    st = hbi_vocabulary_add(*out, "<bos>", 2u);
    if (st != HBI_OK) {
        return st;
    }
    st = hbi_vocabulary_set_special(*out, 2u, HBI_SPECIAL_BOS);
    if (st != HBI_OK) {
        return st;
    }
    st = hbi_vocabulary_add(*out, "<eos>", 3u);
    if (st != HBI_OK) {
        return st;
    }
    st = hbi_vocabulary_set_special(*out, 3u, HBI_SPECIAL_EOS);
    if (st != HBI_OK) {
        return st;
    }
    /* Word tokens. */
    st = hbi_vocabulary_add(*out, "hello", 10u);
    if (st != HBI_OK) {
        return st;
    }
    st = hbi_vocabulary_add(*out, "world", 11u);
    if (st != HBI_OK) {
        return st;
    }
    return HBI_OK;
}

/* ── UTF-8 tests ──────────────────────────────────────────────────────────── */

static void test_utf8_seq_len(void) {
    HBI_CHECK_EQ_INT(hbi_utf8_seq_len(0x41u), 1u); /* 'A' */
    HBI_CHECK_EQ_INT(hbi_utf8_seq_len(0xC2u), 2u); /* 2-byte leading */
    HBI_CHECK_EQ_INT(hbi_utf8_seq_len(0xE0u), 3u); /* 3-byte leading */
    HBI_CHECK_EQ_INT(hbi_utf8_seq_len(0xF0u), 4u); /* 4-byte leading */
    HBI_CHECK_EQ_INT(hbi_utf8_seq_len(0x80u), 0u); /* continuation = invalid */
    HBI_CHECK_EQ_INT(hbi_utf8_seq_len(0xFEu), 0u); /* invalid */
    HBI_CHECK_EQ_INT(hbi_utf8_seq_len(0xFFu), 0u); /* invalid */
}

static void test_utf8_decode_ascii(void) {
    uint8_t src[] = {0x48u}; /* 'H' */
    uint32_t cp;
    uint32_t n = hbi_utf8_decode(src, sizeof(src), &cp);
    HBI_CHECK_EQ_INT(n, 1u);
    HBI_CHECK_EQ_INT(cp, 0x48u);
}

static void test_utf8_decode_multibyte(void) {
    /* U+00E9 (é) = 0xC3 0xA9 */
    uint8_t src[] = {0xC3u, 0xA9u};
    uint32_t cp;
    uint32_t n = hbi_utf8_decode(src, sizeof(src), &cp);
    HBI_CHECK_EQ_INT(n, 2u);
    HBI_CHECK_EQ_INT(cp, 0x00E9u);

    /* U+4E16 (世) = 0xE4 0xB8 0x96 */
    uint8_t src3[] = {0xE4u, 0xB8u, 0x96u};
    n = hbi_utf8_decode(src3, sizeof(src3), &cp);
    HBI_CHECK_EQ_INT(n, 3u);
    HBI_CHECK_EQ_INT(cp, 0x4E16u);

    /* U+1F600 (😀) = 0xF0 0x9F 0x98 0x80 */
    uint8_t src4[] = {0xF0u, 0x9Fu, 0x98u, 0x80u};
    n = hbi_utf8_decode(src4, sizeof(src4), &cp);
    HBI_CHECK_EQ_INT(n, 4u);
    HBI_CHECK_EQ_INT(cp, 0x1F600u);
}

static void test_utf8_decode_invalid(void) {
    uint32_t cp;
    /* Truncated sequence. */
    uint8_t truncated[] = {0xC3u};
    HBI_CHECK_EQ_INT(hbi_utf8_decode(truncated, 1u, &cp), 0u);
    HBI_CHECK_EQ_INT(cp, 0xFFFDu);

    /* Bad continuation. */
    uint8_t bad_cont[] = {0xC3u, 0x41u};
    HBI_CHECK_EQ_INT(hbi_utf8_decode(bad_cont, 2u, &cp), 0u);

    /* Surrogate. */
    uint8_t surrogate[] = {0xEDu, 0xA0u, 0x80u}; /* U+D800 */
    HBI_CHECK_EQ_INT(hbi_utf8_decode(surrogate, 3u, &cp), 0u);
}

static void test_utf8_encode_roundtrip(void) {
    uint8_t buf[4];
    uint32_t codepoints[] = {0x41u, 0x00E9u, 0x4E16u, 0x1F600u};
    for (size_t i = 0u; i < HB_ARRAY_LEN(codepoints); i++) {
        uint32_t enc_len = hbi_utf8_encode(codepoints[i], buf);
        HBI_CHECK(enc_len > 0u);
        uint32_t cp;
        uint32_t dec_len = hbi_utf8_decode(buf, enc_len, &cp);
        HBI_CHECK_EQ_INT(dec_len, enc_len);
        HBI_CHECK_EQ_INT(cp, codepoints[i]);
    }
}

static void test_utf8_validate(void) {
    HBI_CHECK(hbi_utf8_validate((const uint8_t *)"hello", 5u));
    /* Valid 2-byte. */
    uint8_t valid2[] = {0xC3u, 0xA9u, 0x41u};
    HBI_CHECK(hbi_utf8_validate(valid2, sizeof(valid2)));
    /* Invalid: orphan continuation. */
    uint8_t bad[] = {0x41u, 0x80u, 0x41u};
    HBI_CHECK(!hbi_utf8_validate(bad, sizeof(bad)));
    /* Empty is valid. */
    HBI_CHECK(hbi_utf8_validate((const uint8_t *)"", 0u));
}

/* ── Special token type strings ───────────────────────────────────────────── */

static void test_special_token_type_str(void) {
    HBI_CHECK_STR_EQ(hbi_special_token_type_str(HBI_SPECIAL_UNKNOWN), "unknown");
    HBI_CHECK_STR_EQ(hbi_special_token_type_str(HBI_SPECIAL_PAD), "pad");
    HBI_CHECK_STR_EQ(hbi_special_token_type_str(HBI_SPECIAL_BOS), "bos");
    HBI_CHECK_STR_EQ(hbi_special_token_type_str(HBI_SPECIAL_EOS), "eos");
    HBI_CHECK_STR_EQ(hbi_special_token_type_str(HBI_SPECIAL_SEP), "sep");
    HBI_CHECK_STR_EQ(hbi_special_token_type_str(HBI_SPECIAL_CLS), "cls");
    HBI_CHECK_STR_EQ(hbi_special_token_type_str(HBI_SPECIAL_MASK), "mask");
    HBI_CHECK_STR_EQ(hbi_special_token_type_str(HBI_SPECIAL_COUNT), "invalid");
}

/* ── Token sequence tests ─────────────────────────────────────────────────── */

static void test_token_sequence_basic(void) {
    hbi_allocator *a = test_alloc();
    hbi_token_sequence *seq = NULL;
    HBI_CHECK(hbi_token_sequence_create(&seq, a) == HBI_OK);
    HBI_CHECK(seq != NULL);
    HBI_CHECK_EQ_INT(hbi_token_sequence_count(seq), 0u);

    HBI_CHECK(hbi_token_sequence_append(seq, 42u) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_token_sequence_count(seq), 1u);
    HBI_CHECK_EQ_INT(hbi_token_sequence_get(seq, 0u), 42u);

    /* Append many. */
    hbi_token_id ids[] = {10u, 20u, 30u};
    HBI_CHECK(hbi_token_sequence_append_many(seq, ids, 3u) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_token_sequence_count(seq), 4u);
    HBI_CHECK_EQ_INT(hbi_token_sequence_get(seq, 1u), 10u);
    HBI_CHECK_EQ_INT(hbi_token_sequence_get(seq, 3u), 30u);

    /* Out of range. */
    HBI_CHECK_EQ_INT(hbi_token_sequence_get(seq, 99u), HBI_TOKEN_INVALID_ID);

    /* Clear. */
    hbi_token_sequence_clear(seq);
    HBI_CHECK_EQ_INT(hbi_token_sequence_count(seq), 0u);

    /* Data pointer. */
    HBI_CHECK(hbi_token_sequence_data(seq) != NULL);

    hbi_token_sequence_destroy(seq);
    /* NULL-safe. */
    hbi_token_sequence_destroy(NULL);
}

static void test_token_sequence_grow(void) {
    hbi_allocator *a = test_alloc();
    hbi_token_sequence *seq = NULL;
    HBI_CHECK(hbi_token_sequence_create(&seq, a) == HBI_OK);
    /* Append more than initial capacity to trigger grow. */
    for (uint32_t i = 0u; i < 200u; i++) {
        HBI_CHECK(hbi_token_sequence_append(seq, i) == HBI_OK);
    }
    HBI_CHECK_EQ_INT(hbi_token_sequence_count(seq), 200u);
    for (uint32_t i = 0u; i < 200u; i++) {
        HBI_CHECK_EQ_INT(hbi_token_sequence_get(seq, i), i);
    }
    hbi_token_sequence_destroy(seq);
}

/* ── Vocabulary tests ─────────────────────────────────────────────────────── */

static void test_vocabulary_basic(void) {
    hbi_vocabulary *v = NULL;
    HBI_CHECK(hbi_vocabulary_create(&v, test_alloc(), 32u) == HBI_OK);
    HBI_CHECK(v != NULL);
    HBI_CHECK_EQ_INT(hbi_vocabulary_size(v), 0u);

    HBI_CHECK(hbi_vocabulary_add(v, "hello", 10u) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_vocabulary_size(v), 1u);
    HBI_CHECK_EQ_INT(hbi_vocabulary_lookup(v, "hello"), 10u);
    HBI_CHECK_STR_EQ(hbi_vocabulary_text(v, 10u), "hello");

    HBI_CHECK_EQ_INT(hbi_vocabulary_lookup(v, "missing"), HBI_TOKEN_INVALID_ID);
    HBI_CHECK(hbi_vocabulary_text(v, 999u) == NULL);

    hbi_vocabulary_destroy(v);
    hbi_vocabulary_destroy(NULL); /* NULL-safe */
}

static void test_vocabulary_special_tokens(void) {
    hbi_vocabulary *v = NULL;
    HBI_CHECK(hbi_vocabulary_create(&v, test_alloc(), 16u) == HBI_OK);
    HBI_CHECK(hbi_vocabulary_add(v, "<unk>", 0u) == HBI_OK);
    HBI_CHECK(hbi_vocabulary_set_special(v, 0u, HBI_SPECIAL_UNKNOWN) == HBI_OK);
    HBI_CHECK(hbi_vocabulary_is_special(v, 0u));
    HBI_CHECK(!hbi_vocabulary_is_special(v, 99u));
    HBI_CHECK_EQ_INT(hbi_vocabulary_special_id(v, HBI_SPECIAL_UNKNOWN), 0u);
    HBI_CHECK_EQ_INT(hbi_vocabulary_special_id(v, HBI_SPECIAL_EOS), HBI_TOKEN_INVALID_ID);

    /* Set special on missing ID. */
    HBI_CHECK(hbi_vocabulary_set_special(v, 999u, HBI_SPECIAL_PAD) == HBI_ERR_NOT_FOUND);
    hbi_vocabulary_destroy(v);
}

static void test_vocabulary_duplicates(void) {
    hbi_vocabulary *v = NULL;
    HBI_CHECK(hbi_vocabulary_create(&v, test_alloc(), 16u) == HBI_OK);
    HBI_CHECK(hbi_vocabulary_add(v, "hello", 10u) == HBI_OK);
    /* Exact duplicate is idempotent. */
    HBI_CHECK(hbi_vocabulary_add(v, "hello", 10u) == HBI_OK);
    /* Same ID, different text. */
    HBI_CHECK(hbi_vocabulary_add(v, "world", 10u) == HBI_ERR_STATE);
    /* Same text, different ID. */
    HBI_CHECK(hbi_vocabulary_add(v, "hello", 11u) == HBI_ERR_STATE);
    hbi_vocabulary_destroy(v);
}

static void test_vocabulary_validate(void) {
    hbi_vocabulary *v = NULL;
    HBI_CHECK(hbi_vocabulary_create(&v, test_alloc(), 16u) == HBI_OK);
    /* Missing unknown token. */
    HBI_CHECK(hbi_vocabulary_validate(v) == HBI_ERR_STATE);
    /* Add unknown. */
    HBI_CHECK(hbi_vocabulary_add(v, "<unk>", 0u) == HBI_OK);
    HBI_CHECK(hbi_vocabulary_set_special(v, 0u, HBI_SPECIAL_UNKNOWN) == HBI_OK);
    HBI_CHECK(hbi_vocabulary_validate(v) == HBI_OK);
    hbi_vocabulary_destroy(v);
}

static void test_vocabulary_large(void) {
    hbi_vocabulary *v = NULL;
    HBI_CHECK(hbi_vocabulary_create(&v, test_alloc(), 16u) == HBI_OK);
    /* Add 200 tokens to exercise hash-table resizing. */
    char buf[32];
    for (uint32_t i = 0u; i < 200u; i++) {
        snprintf(buf, sizeof(buf), "token_%u", i);
        HBI_CHECK(hbi_vocabulary_add(v, buf, i) == HBI_OK);
    }
    HBI_CHECK_EQ_INT(hbi_vocabulary_size(v), 200u);
    /* Verify all lookups. */
    for (uint32_t i = 0u; i < 200u; i++) {
        snprintf(buf, sizeof(buf), "token_%u", i);
        HBI_CHECK_EQ_INT(hbi_vocabulary_lookup(v, buf), i);
    }
    hbi_vocabulary_destroy(v);
}

/* ── Decode state tests ───────────────────────────────────────────────────── */

static void test_decode_state(void) {
    hbi_decode_state *s = NULL;
    HBI_CHECK(hbi_decode_state_create(&s, test_alloc()) == HBI_OK);
    HBI_CHECK(s != NULL);
    HBI_CHECK_EQ_INT(s->pending_len, 0u);
    hbi_decode_state_reset(s);
    HBI_CHECK_EQ_INT(s->pending_len, 0u);
    hbi_decode_state_destroy(s);
    hbi_decode_state_destroy(NULL);
}

/* ── Registry tests ───────────────────────────────────────────────────────── */

static void test_registry_basic(void) {
    hbi_tokenizer_registry_clear();
    HBI_CHECK_EQ_INT(hbi_tokenizer_count(), 0);

    HBI_CHECK(hbi_tokenizer_mock_register() == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_tokenizer_count(), 1);

    const hbi_tokenizer *t = hbi_tokenizer_find("mock");
    HBI_CHECK(t != NULL);
    HBI_CHECK_STR_EQ(t->name, "mock");

    HBI_CHECK(hbi_tokenizer_find("nonexistent") == NULL);

    /* Duplicate registration. */
    HBI_CHECK(hbi_tokenizer_mock_register() == HBI_ERR_STATE);
    hbi_tokenizer_registry_clear();
}

static void test_registry_invalid(void) {
    hbi_tokenizer_registry_clear();
    /* NULL tokenizer. */
    HBI_CHECK(hbi_tokenizer_register(NULL) == HBI_ERR_INVALID_ARG);
    /* Missing required fields. */
    hbi_tokenizer incomplete;
    memset(&incomplete, 0, sizeof(incomplete));
    HBI_CHECK(hbi_tokenizer_register(&incomplete) == HBI_ERR_INVALID_ARG);
    hbi_tokenizer_registry_clear();
}

/* ── Manager tests ────────────────────────────────────────────────────────── */

static void test_manager_encode_decode(void) {
    hbi_tokenizer_registry_clear();
    HBI_CHECK(hbi_tokenizer_mock_register() == HBI_OK);

    hbi_vocabulary *vocab = NULL;
    HBI_CHECK(setup_test_vocab(&vocab) == HBI_OK);

    const hbi_tokenizer *tok = hbi_tokenizer_find("mock");
    HBI_CHECK(tok != NULL);

    hbi_tokenizer_manager *mgr = NULL;
    HBI_CHECK(hbi_tokenizer_manager_create(&mgr, tok, vocab, test_alloc()) == HBI_OK);
    HBI_CHECK(mgr != NULL);

    /* Encode. */
    hbi_token_sequence *seq = NULL;
    HBI_CHECK(hbi_token_sequence_create(&seq, test_alloc()) == HBI_OK);
    const char *input = "Hi";
    HBI_CHECK(hbi_tokenizer_manager_encode(mgr, input, strlen(input), seq) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_token_sequence_count(seq), 2u);
    /* 'H' = 0x48 + 5 = 77, 'i' = 0x69 + 5 = 110 */
    HBI_CHECK_EQ_INT(hbi_token_sequence_get(seq, 0u), 77u);
    HBI_CHECK_EQ_INT(hbi_token_sequence_get(seq, 1u), 110u);

    /* Decode. */
    char out[64];
    size_t out_len = 0u;
    HBI_CHECK(hbi_tokenizer_manager_decode(mgr, hbi_token_sequence_data(seq),
                                           hbi_token_sequence_count(seq), out, sizeof(out),
                                           &out_len) == HBI_OK);
    HBI_CHECK_EQ_INT(out_len, 2u);
    HBI_CHECK_STR_EQ(out, "Hi");

    hbi_token_sequence_destroy(seq);
    hbi_tokenizer_manager_destroy(mgr);
    hbi_vocabulary_destroy(vocab);
    hbi_tokenizer_registry_clear();
}

static void test_manager_empty_input(void) {
    hbi_tokenizer_registry_clear();
    HBI_CHECK(hbi_tokenizer_mock_register() == HBI_OK);

    hbi_vocabulary *vocab = NULL;
    HBI_CHECK(setup_test_vocab(&vocab) == HBI_OK);

    const hbi_tokenizer *tok = hbi_tokenizer_find("mock");
    hbi_tokenizer_manager *mgr = NULL;
    HBI_CHECK(hbi_tokenizer_manager_create(&mgr, tok, vocab, test_alloc()) == HBI_OK);

    hbi_token_sequence *seq = NULL;
    HBI_CHECK(hbi_token_sequence_create(&seq, test_alloc()) == HBI_OK);

    /* Empty string encode. */
    HBI_CHECK(hbi_tokenizer_manager_encode(mgr, "", 0u, seq) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_token_sequence_count(seq), 0u);

    /* Empty token decode. */
    char out[8];
    size_t out_len = 0u;
    HBI_CHECK(hbi_tokenizer_manager_decode(mgr, NULL, 0u, out, sizeof(out), &out_len) == HBI_OK);
    HBI_CHECK_EQ_INT(out_len, 0u);

    hbi_token_sequence_destroy(seq);
    hbi_tokenizer_manager_destroy(mgr);
    hbi_vocabulary_destroy(vocab);
    hbi_tokenizer_registry_clear();
}

static void test_manager_incremental_encode(void) {
    hbi_tokenizer_registry_clear();
    HBI_CHECK(hbi_tokenizer_mock_register() == HBI_OK);

    hbi_vocabulary *vocab = NULL;
    HBI_CHECK(setup_test_vocab(&vocab) == HBI_OK);

    const hbi_tokenizer *tok = hbi_tokenizer_find("mock");
    hbi_tokenizer_manager *mgr = NULL;
    HBI_CHECK(hbi_tokenizer_manager_create(&mgr, tok, vocab, test_alloc()) == HBI_OK);

    hbi_token_sequence *seq = NULL;
    HBI_CHECK(hbi_token_sequence_create(&seq, test_alloc()) == HBI_OK);

    /* Incremental: send "He" then "llo". */
    HBI_CHECK(hbi_tokenizer_manager_encode_incremental(mgr, "He", 2u, seq) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_token_sequence_count(seq), 2u);
    HBI_CHECK(hbi_tokenizer_manager_encode_incremental(mgr, "llo", 3u, seq) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_token_sequence_count(seq), 5u);

    hbi_token_sequence_destroy(seq);
    hbi_tokenizer_manager_destroy(mgr);
    hbi_vocabulary_destroy(vocab);
    hbi_tokenizer_registry_clear();
}

static void test_manager_incremental_decode(void) {
    hbi_tokenizer_registry_clear();
    HBI_CHECK(hbi_tokenizer_mock_register() == HBI_OK);

    hbi_vocabulary *vocab = NULL;
    HBI_CHECK(setup_test_vocab(&vocab) == HBI_OK);

    const hbi_tokenizer *tok = hbi_tokenizer_find("mock");
    hbi_tokenizer_manager *mgr = NULL;
    HBI_CHECK(hbi_tokenizer_manager_create(&mgr, tok, vocab, test_alloc()) == HBI_OK);

    /* Decode 'A' (0x41 + 5 = 70) and 'B' (0x42 + 5 = 71) incrementally. */
    char out[8];
    size_t out_len = 0u;
    HBI_CHECK(hbi_tokenizer_manager_decode_incremental(mgr, 70u, out, sizeof(out), &out_len) ==
              HBI_OK);
    HBI_CHECK_EQ_INT(out_len, 1u);
    HBI_CHECK_EQ_INT(out[0], 'A');

    HBI_CHECK(hbi_tokenizer_manager_decode_incremental(mgr, 71u, out, sizeof(out), &out_len) ==
              HBI_OK);
    HBI_CHECK_EQ_INT(out_len, 1u);
    HBI_CHECK_EQ_INT(out[0], 'B');

    /* Special token (ID 2 = BOS) should produce no output. */
    HBI_CHECK(hbi_tokenizer_manager_decode_incremental(mgr, 2u, out, sizeof(out), &out_len) ==
              HBI_OK);
    HBI_CHECK_EQ_INT(out_len, 0u);

    hbi_tokenizer_manager_destroy(mgr);
    hbi_vocabulary_destroy(vocab);
    hbi_tokenizer_registry_clear();
}

static void test_manager_statistics(void) {
    hbi_tokenizer_registry_clear();
    HBI_CHECK(hbi_tokenizer_mock_register() == HBI_OK);

    hbi_vocabulary *vocab = NULL;
    HBI_CHECK(setup_test_vocab(&vocab) == HBI_OK);

    const hbi_tokenizer *tok = hbi_tokenizer_find("mock");
    hbi_tokenizer_manager *mgr = NULL;
    HBI_CHECK(hbi_tokenizer_manager_create(&mgr, tok, vocab, test_alloc()) == HBI_OK);

    hbi_token_sequence *seq = NULL;
    HBI_CHECK(hbi_token_sequence_create(&seq, test_alloc()) == HBI_OK);

    HBI_CHECK(hbi_tokenizer_manager_encode(mgr, "abc", 3u, seq) == HBI_OK);
    char out[16];
    size_t out_len = 0u;
    HBI_CHECK(hbi_tokenizer_manager_decode(mgr, hbi_token_sequence_data(seq),
                                           hbi_token_sequence_count(seq), out, sizeof(out),
                                           &out_len) == HBI_OK);

    hbi_tokenizer_statistics stats;
    HBI_CHECK(hbi_tokenizer_manager_get_statistics(mgr, &stats) == HBI_OK);
    HBI_CHECK_EQ_INT(stats.encode_calls, 1u);
    HBI_CHECK_EQ_INT(stats.decode_calls, 1u);
    HBI_CHECK(stats.tokens_encoded >= 3u);
    HBI_CHECK(stats.tokens_decoded >= 3u);

    hbi_token_sequence_destroy(seq);
    hbi_tokenizer_manager_destroy(mgr);
    hbi_vocabulary_destroy(vocab);
    hbi_tokenizer_registry_clear();
}

static void test_manager_reset(void) {
    hbi_tokenizer_registry_clear();
    HBI_CHECK(hbi_tokenizer_mock_register() == HBI_OK);

    hbi_vocabulary *vocab = NULL;
    HBI_CHECK(setup_test_vocab(&vocab) == HBI_OK);

    const hbi_tokenizer *tok = hbi_tokenizer_find("mock");
    hbi_tokenizer_manager *mgr = NULL;
    HBI_CHECK(hbi_tokenizer_manager_create(&mgr, tok, vocab, test_alloc()) == HBI_OK);

    hbi_token_sequence *seq = NULL;
    HBI_CHECK(hbi_token_sequence_create(&seq, test_alloc()) == HBI_OK);

    /* Encode incrementally to create context. */
    HBI_CHECK(hbi_tokenizer_manager_encode_incremental(mgr, "x", 1u, seq) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_token_sequence_count(seq), 1u);

    /* Reset should clear encode context and decode state. */
    hbi_tokenizer_manager_reset(mgr);

    /* Accessors. */
    HBI_CHECK(hbi_tokenizer_manager_vocabulary(mgr) == vocab);
    HBI_CHECK(hbi_tokenizer_manager_tokenizer(mgr) == tok);

    hbi_token_sequence_destroy(seq);
    hbi_tokenizer_manager_destroy(mgr);
    hbi_vocabulary_destroy(vocab);
    hbi_tokenizer_registry_clear();
}

/* ── Error handling tests ─────────────────────────────────────────────────── */

static void test_null_args(void) {
    /* Token sequence. */
    HBI_CHECK(hbi_token_sequence_create(NULL, test_alloc()) == HBI_ERR_INVALID_ARG);
    HBI_CHECK(hbi_token_sequence_append(NULL, 0u) == HBI_ERR_INVALID_ARG);

    /* Vocabulary. */
    HBI_CHECK(hbi_vocabulary_create(NULL, test_alloc(), 16u) == HBI_ERR_INVALID_ARG);
    HBI_CHECK(hbi_vocabulary_add(NULL, "x", 0u) == HBI_ERR_INVALID_ARG);
    hbi_vocabulary v_dummy;
    memset(&v_dummy, 0, sizeof(v_dummy));
    HBI_CHECK_EQ_INT(hbi_vocabulary_lookup(&v_dummy, NULL), HBI_TOKEN_INVALID_ID);

    /* Decode state. */
    HBI_CHECK(hbi_decode_state_create(NULL, test_alloc()) == HBI_ERR_INVALID_ARG);

    /* Manager. */
    HBI_CHECK(hbi_tokenizer_manager_create(NULL, NULL, NULL, NULL) == HBI_ERR_INVALID_ARG);

    /* Statistics. */
    HBI_CHECK(hbi_tokenizer_manager_get_statistics(NULL, NULL) == HBI_ERR_INVALID_ARG);
}

/* ── Selftest ─────────────────────────────────────────────────────────────── */

static void test_selftest(void) {
    HBI_CHECK(hbi_tokenizer_selftest() == HBI_OK);
    HBI_CHECK_STR_EQ(hbi_tokenizer_name(), "tokenizer");
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void) {
    HBI_TEST_BEGIN("tokenizer");

    /* UTF-8. */
    HBI_RUN(test_utf8_seq_len);
    HBI_RUN(test_utf8_decode_ascii);
    HBI_RUN(test_utf8_decode_multibyte);
    HBI_RUN(test_utf8_decode_invalid);
    HBI_RUN(test_utf8_encode_roundtrip);
    HBI_RUN(test_utf8_validate);

    /* Special token types. */
    HBI_RUN(test_special_token_type_str);

    /* Token sequence. */
    HBI_RUN(test_token_sequence_basic);
    HBI_RUN(test_token_sequence_grow);

    /* Vocabulary. */
    HBI_RUN(test_vocabulary_basic);
    HBI_RUN(test_vocabulary_special_tokens);
    HBI_RUN(test_vocabulary_duplicates);
    HBI_RUN(test_vocabulary_validate);
    HBI_RUN(test_vocabulary_large);

    /* Decode state. */
    HBI_RUN(test_decode_state);

    /* Registry. */
    HBI_RUN(test_registry_basic);
    HBI_RUN(test_registry_invalid);

    /* Manager. */
    HBI_RUN(test_manager_encode_decode);
    HBI_RUN(test_manager_empty_input);
    HBI_RUN(test_manager_incremental_encode);
    HBI_RUN(test_manager_incremental_decode);
    HBI_RUN(test_manager_statistics);
    HBI_RUN(test_manager_reset);

    /* Error handling. */
    HBI_RUN(test_null_args);

    /* Selftest. */
    HBI_RUN(test_selftest);

    return HBI_TEST_END();
}

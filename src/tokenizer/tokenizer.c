/* tokenizer.c — Tokenizer Framework implementation (RFC-013).
 *
 * UTF-8 utilities, token sequence, vocabulary (FNV-1a open-addressing hash
 * table), decode state, tokenizer registry, and tokenizer manager.
 */
#include "tokenizer/tokenizer_internal.h"

#include <string.h>

/* ── UTF-8 utilities ──────────────────────────────────────────────────────── */

uint32_t hbi_utf8_seq_len(uint8_t b) {
    if ((b & 0x80u) == 0u) {
        return 1u; /* 0xxxxxxx */
    }
    if ((b & 0xE0u) == 0xC0u) {
        return 2u; /* 110xxxxx */
    }
    if ((b & 0xF0u) == 0xE0u) {
        return 3u; /* 1110xxxx */
    }
    if ((b & 0xF8u) == 0xF0u) {
        return 4u; /* 11110xxx */
    }
    return 0u; /* invalid leading byte */
}

uint32_t hbi_utf8_decode(const uint8_t *src, size_t src_len, uint32_t *out_cp) {
    if (!src || src_len == 0u || !out_cp) {
        if (out_cp) {
            *out_cp = 0xFFFDu;
        }
        return 0u;
    }
    uint32_t len = hbi_utf8_seq_len(src[0]);
    if (len == 0u || (size_t)len > src_len) {
        *out_cp = 0xFFFDu;
        return 0u;
    }
    uint32_t cp;
    switch (len) {
    case 1u:
        cp = src[0];
        break;
    case 2u:
        cp = (uint32_t)(src[0] & 0x1Fu) << 6u;
        cp |= (uint32_t)(src[1] & 0x3Fu);
        break;
    case 3u:
        cp = (uint32_t)(src[0] & 0x0Fu) << 12u;
        cp |= (uint32_t)(src[1] & 0x3Fu) << 6u;
        cp |= (uint32_t)(src[2] & 0x3Fu);
        break;
    case 4u:
        cp = (uint32_t)(src[0] & 0x07u) << 18u;
        cp |= (uint32_t)(src[1] & 0x3Fu) << 12u;
        cp |= (uint32_t)(src[2] & 0x3Fu) << 6u;
        cp |= (uint32_t)(src[3] & 0x3Fu);
        break;
    default:
        *out_cp = 0xFFFDu;
        return 0u;
    }
    /* Validate continuation bytes. */
    for (uint32_t i = 1u; i < len; i++) {
        if ((src[i] & 0xC0u) != 0x80u) {
            *out_cp = 0xFFFDu;
            return 0u;
        }
    }
    /* Reject overlong encodings and surrogates. */
    if (cp < 0x80u && len != 1u) {
        *out_cp = 0xFFFDu;
        return 0u;
    }
    if (cp >= 0xD800u && cp <= 0xDFFFu) {
        *out_cp = 0xFFFDu;
        return 0u;
    }
    if (cp > 0x10FFFFu) {
        *out_cp = 0xFFFDu;
        return 0u;
    }
    *out_cp = cp;
    return len;
}

uint32_t hbi_utf8_encode(uint32_t cp, uint8_t *dst) {
    if (!dst || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
        return 0u;
    }
    if (cp < 0x80u) {
        dst[0] = (uint8_t)cp;
        return 1u;
    }
    if (cp < 0x800u) {
        dst[0] = (uint8_t)(0xC0u | (cp >> 6u));
        dst[1] = (uint8_t)(0x80u | (cp & 0x3Fu));
        return 2u;
    }
    if (cp < 0x10000u) {
        dst[0] = (uint8_t)(0xE0u | (cp >> 12u));
        dst[1] = (uint8_t)(0x80u | ((cp >> 6u) & 0x3Fu));
        dst[2] = (uint8_t)(0x80u | (cp & 0x3Fu));
        return 3u;
    }
    dst[0] = (uint8_t)(0xF0u | (cp >> 18u));
    dst[1] = (uint8_t)(0x80u | ((cp >> 12u) & 0x3Fu));
    dst[2] = (uint8_t)(0x80u | ((cp >> 6u) & 0x3Fu));
    dst[3] = (uint8_t)(0x80u | (cp & 0x3Fu));
    return 4u;
}

bool hbi_utf8_validate(const uint8_t *text, size_t len) {
    if (!text && len > 0u) {
        return false;
    }
    size_t i = 0u;
    while (i < len) {
        uint32_t cp;
        uint32_t consumed = hbi_utf8_decode(text + i, len - i, &cp);
        if (consumed == 0u) {
            return false;
        }
        i += (size_t)consumed;
    }
    return true;
}

/* ── Special token type strings ───────────────────────────────────────────── */

const char *hbi_special_token_type_str(hbi_special_token_type t) {
    switch (t) {
    case HBI_SPECIAL_UNKNOWN:
        return "unknown";
    case HBI_SPECIAL_PAD:
        return "pad";
    case HBI_SPECIAL_BOS:
        return "bos";
    case HBI_SPECIAL_EOS:
        return "eos";
    case HBI_SPECIAL_SEP:
        return "sep";
    case HBI_SPECIAL_CLS:
        return "cls";
    case HBI_SPECIAL_MASK:
        return "mask";
    default:
        return "invalid";
    }
}

/* ── Token sequence ───────────────────────────────────────────────────────── */

hbi_status hbi_token_sequence_create(hbi_token_sequence **out, hbi_allocator *allocator) {
    if (!out || !allocator) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL out or allocator");
    }
    hbi_token_sequence *seq =
        (hbi_token_sequence *)hbi_alloc(allocator, sizeof(hbi_token_sequence), 0, HBI_MEM_GENERAL);
    if (!seq) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "token sequence allocation failed");
    }
    seq->allocator = allocator;
    seq->count = 0u;
    seq->capacity = HBI_TOKEN_SEQ_INITIAL_CAP;
    seq->data = (hbi_token_id *)hbi_alloc(allocator, (size_t)seq->capacity * sizeof(hbi_token_id),
                                          0, HBI_MEM_GENERAL);
    if (!seq->data) {
        hbi_free(allocator, seq);
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "token sequence data allocation failed");
    }
    *out = seq;
    return HBI_OK;
}

void hbi_token_sequence_destroy(hbi_token_sequence *seq) {
    if (!seq) {
        return;
    }
    hbi_allocator *a = seq->allocator;
    hbi_free(a, seq->data);
    hbi_free(a, seq);
}

static hbi_status token_sequence_grow(hbi_token_sequence *seq, uint32_t needed) {
    if (seq->count + needed <= seq->capacity) {
        return HBI_OK;
    }
    uint32_t new_cap = seq->capacity;
    while (new_cap < seq->count + needed) {
        new_cap *= 2u;
    }
    hbi_token_id *new_data = (hbi_token_id *)hbi_realloc(
        seq->allocator, seq->data, (size_t)new_cap * sizeof(hbi_token_id), 0, HBI_MEM_GENERAL);
    if (!new_data) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "token sequence grow failed");
    }
    seq->data = new_data;
    seq->capacity = new_cap;
    return HBI_OK;
}

hbi_status hbi_token_sequence_append(hbi_token_sequence *seq, hbi_token_id id) {
    if (!seq) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL sequence");
    }
    hbi_status st = token_sequence_grow(seq, 1u);
    if (st != HBI_OK) {
        return st;
    }
    seq->data[seq->count++] = id;
    return HBI_OK;
}

hbi_status hbi_token_sequence_append_many(hbi_token_sequence *seq, const hbi_token_id *ids,
                                          size_t count) {
    if (!seq || (!ids && count > 0u)) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL sequence or ids");
    }
    if (count == 0u) {
        return HBI_OK;
    }
    hbi_status st = token_sequence_grow(seq, (uint32_t)count);
    if (st != HBI_OK) {
        return st;
    }
    memcpy(seq->data + seq->count, ids, count * sizeof(hbi_token_id));
    seq->count += (uint32_t)count;
    return HBI_OK;
}

uint32_t hbi_token_sequence_count(const hbi_token_sequence *seq) {
    return seq ? seq->count : 0u;
}

hbi_token_id hbi_token_sequence_get(const hbi_token_sequence *seq, uint32_t index) {
    if (!seq || index >= seq->count) {
        return HBI_TOKEN_INVALID_ID;
    }
    return seq->data[index];
}

const hbi_token_id *hbi_token_sequence_data(const hbi_token_sequence *seq) {
    return seq ? seq->data : NULL;
}

void hbi_token_sequence_clear(hbi_token_sequence *seq) {
    if (seq) {
        seq->count = 0u;
    }
}

/* ── Vocabulary ───────────────────────────────────────────────────────────── */

/* FNV-1a hash for token text. */
static size_t vocab_hash(const char *text, size_t capacity) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const char *p = text; *p; p++) {
        h ^= (uint64_t)(uint8_t)*p;
        h *= 0x100000001b3ULL;
    }
    return (size_t)(h & (uint64_t)(capacity - 1u));
}

static size_t vocab_next_pow2(size_t n) {
    size_t cap = 16u;
    while (cap < n) {
        cap *= 2u;
    }
    return cap;
}

hbi_status hbi_vocabulary_create(hbi_vocabulary **out, hbi_allocator *allocator,
                                 size_t initial_capacity) {
    if (!out || !allocator) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL out or allocator");
    }
    if (initial_capacity < 16u) {
        initial_capacity = 16u;
    }
    size_t cap = vocab_next_pow2(initial_capacity);

    hbi_vocabulary *v =
        (hbi_vocabulary *)hbi_alloc(allocator, sizeof(hbi_vocabulary), 0, HBI_MEM_GENERAL);
    if (!v) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "vocabulary allocation failed");
    }
    v->table =
        (hbi_vocab_entry *)hbi_alloc(allocator, cap * sizeof(hbi_vocab_entry), 0, HBI_MEM_GENERAL);
    if (!v->table) {
        hbi_free(allocator, v);
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "vocabulary table allocation failed");
    }
    memset(v->table, 0, cap * sizeof(hbi_vocab_entry));
    v->table_capacity = cap;
    v->count = 0u;
    v->allocator = allocator;
    for (int i = 0; i < HBI_SPECIAL_COUNT; i++) {
        v->special_ids[i] = HBI_TOKEN_INVALID_ID;
    }
    *out = v;
    return HBI_OK;
}

void hbi_vocabulary_destroy(hbi_vocabulary *vocab) {
    if (!vocab) {
        return;
    }
    hbi_allocator *a = vocab->allocator;
    hbi_free(a, vocab->table);
    hbi_free(a, vocab);
}

/* Find slot by text (for lookup). Returns the slot index, or table_capacity if not found. */
static size_t vocab_find_slot(const hbi_vocabulary *vocab, const char *text) {
    size_t idx = vocab_hash(text, vocab->table_capacity);
    for (size_t i = 0u; i < vocab->table_capacity; i++) {
        size_t slot = (idx + i) & (vocab->table_capacity - 1u);
        if (!vocab->table[slot].occupied) {
            return vocab->table_capacity; /* not found */
        }
        if (strcmp(vocab->table[slot].text, text) == 0) {
            return slot;
        }
    }
    return vocab->table_capacity;
}

/* Find slot by ID (linear scan — acceptable for framework-level vocabulary sizes). */
static size_t vocab_find_by_id(const hbi_vocabulary *vocab, hbi_token_id id) {
    for (size_t i = 0u; i < vocab->table_capacity; i++) {
        if (vocab->table[i].occupied && vocab->table[i].id == id) {
            return i;
        }
    }
    return vocab->table_capacity;
}

static hbi_status vocab_resize(hbi_vocabulary *vocab) {
    size_t new_cap = vocab->table_capacity * 2u;
    hbi_vocab_entry *new_table = (hbi_vocab_entry *)hbi_alloc(
        vocab->allocator, new_cap * sizeof(hbi_vocab_entry), 0, HBI_MEM_GENERAL);
    if (!new_table) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "vocabulary resize failed");
    }
    memset(new_table, 0, new_cap * sizeof(hbi_vocab_entry));
    for (size_t i = 0u; i < vocab->table_capacity; i++) {
        if (!vocab->table[i].occupied) {
            continue;
        }
        size_t idx = vocab_hash(vocab->table[i].text, new_cap);
        for (size_t j = 0u; j < new_cap; j++) {
            size_t slot = (idx + j) & (new_cap - 1u);
            if (!new_table[slot].occupied) {
                new_table[slot] = vocab->table[i];
                break;
            }
        }
    }
    hbi_free(vocab->allocator, vocab->table);
    vocab->table = new_table;
    vocab->table_capacity = new_cap;
    return HBI_OK;
}

hbi_status hbi_vocabulary_add(hbi_vocabulary *vocab, const char *text, hbi_token_id id) {
    if (!vocab || !text) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL vocab or text");
    }
    size_t text_len = strlen(text);
    if (text_len == 0u || text_len >= HBI_TOKEN_TEXT_MAX) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "empty or too-long token text");
    }
    /* Check for duplicate ID. */
    size_t existing = vocab_find_by_id(vocab, id);
    if (existing < vocab->table_capacity) {
        if (strcmp(vocab->table[existing].text, text) != 0) {
            return HBI_ERR_SET(HBI_ERR_STATE, 0, "duplicate token ID with different text");
        }
        return HBI_OK; /* exact duplicate — idempotent */
    }
    /* Check for duplicate text. */
    size_t text_slot = vocab_find_slot(vocab, text);
    if (text_slot < vocab->table_capacity) {
        if (vocab->table[text_slot].id != id) {
            return HBI_ERR_SET(HBI_ERR_STATE, 0, "duplicate token text with different ID");
        }
        return HBI_OK; /* exact duplicate */
    }
    /* Grow if load factor > 0.7. */
    if ((size_t)vocab->count * 10u > vocab->table_capacity * 7u) {
        hbi_status st = vocab_resize(vocab);
        if (st != HBI_OK) {
            return st;
        }
    }
    /* Insert. */
    size_t idx = vocab_hash(text, vocab->table_capacity);
    for (size_t i = 0u; i < vocab->table_capacity; i++) {
        size_t slot = (idx + i) & (vocab->table_capacity - 1u);
        if (!vocab->table[slot].occupied) {
            memset(&vocab->table[slot], 0, sizeof(hbi_vocab_entry));
            memcpy(vocab->table[slot].text, text, text_len + 1u);
            vocab->table[slot].id = id;
            vocab->table[slot].occupied = true;
            vocab->table[slot].is_special = false;
            vocab->table[slot].special_type = HBI_SPECIAL_UNKNOWN;
            vocab->count++;
            return HBI_OK;
        }
    }
    return HBI_ERR_SET(HBI_ERR_STATE, 0, "vocabulary table full (unexpected)");
}

hbi_status hbi_vocabulary_set_special(hbi_vocabulary *vocab, hbi_token_id id,
                                      hbi_special_token_type type) {
    if (!vocab || type < HBI_SPECIAL_UNKNOWN || type >= HBI_SPECIAL_COUNT) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL vocab or invalid special type");
    }
    size_t slot = vocab_find_by_id(vocab, id);
    if (slot >= vocab->table_capacity) {
        return HBI_ERR_SET(HBI_ERR_NOT_FOUND, 0, "token ID not in vocabulary");
    }
    vocab->table[slot].is_special = true;
    vocab->table[slot].special_type = type;
    vocab->special_ids[type] = id;
    return HBI_OK;
}

hbi_token_id hbi_vocabulary_lookup(const hbi_vocabulary *vocab, const char *text) {
    if (!vocab || !text) {
        return HBI_TOKEN_INVALID_ID;
    }
    size_t slot = vocab_find_slot(vocab, text);
    if (slot >= vocab->table_capacity) {
        return HBI_TOKEN_INVALID_ID;
    }
    return vocab->table[slot].id;
}

const char *hbi_vocabulary_text(const hbi_vocabulary *vocab, hbi_token_id id) {
    if (!vocab) {
        return NULL;
    }
    size_t slot = vocab_find_by_id(vocab, id);
    if (slot >= vocab->table_capacity) {
        return NULL;
    }
    return vocab->table[slot].text;
}

uint32_t hbi_vocabulary_size(const hbi_vocabulary *vocab) {
    return vocab ? vocab->count : 0u;
}

bool hbi_vocabulary_is_special(const hbi_vocabulary *vocab, hbi_token_id id) {
    if (!vocab) {
        return false;
    }
    size_t slot = vocab_find_by_id(vocab, id);
    if (slot >= vocab->table_capacity) {
        return false;
    }
    return vocab->table[slot].is_special;
}

hbi_token_id hbi_vocabulary_special_id(const hbi_vocabulary *vocab, hbi_special_token_type type) {
    if (!vocab || type < HBI_SPECIAL_UNKNOWN || type >= HBI_SPECIAL_COUNT) {
        return HBI_TOKEN_INVALID_ID;
    }
    return vocab->special_ids[type];
}

hbi_status hbi_vocabulary_validate(const hbi_vocabulary *vocab) {
    if (!vocab) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL vocabulary");
    }
    /* Check for well-formed UTF-8 in all token texts. */
    for (size_t i = 0u; i < vocab->table_capacity; i++) {
        if (!vocab->table[i].occupied) {
            continue;
        }
        if (!hbi_utf8_validate((const uint8_t *)vocab->table[i].text,
                               strlen(vocab->table[i].text))) {
            return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "invalid UTF-8 in token '%s'",
                                vocab->table[i].text);
        }
    }
    /* Unknown token should be configured. */
    if (vocab->special_ids[HBI_SPECIAL_UNKNOWN] == HBI_TOKEN_INVALID_ID) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "vocabulary missing unknown token");
    }
    return HBI_OK;
}

/* ── Decode state ─────────────────────────────────────────────────────────── */

hbi_status hbi_decode_state_create(hbi_decode_state **out, hbi_allocator *allocator) {
    if (!out || !allocator) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL out or allocator");
    }
    hbi_decode_state *s =
        (hbi_decode_state *)hbi_alloc(allocator, sizeof(hbi_decode_state), 0, HBI_MEM_GENERAL);
    if (!s) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "decode state allocation failed");
    }
    memset(s->pending, 0, sizeof(s->pending));
    s->pending_len = 0u;
    s->allocator = allocator;
    *out = s;
    return HBI_OK;
}

void hbi_decode_state_destroy(hbi_decode_state *state) {
    if (!state) {
        return;
    }
    hbi_free(state->allocator, state);
}

void hbi_decode_state_reset(hbi_decode_state *state) {
    if (state) {
        memset(state->pending, 0, sizeof(state->pending));
        state->pending_len = 0u;
    }
}

/* ── Tokenizer registry ───────────────────────────────────────────────────── */

static const hbi_tokenizer *g_tokenizer_registry[HBI_TOKENIZER_REGISTRY_MAX];
static uint32_t g_tokenizer_registry_count = 0u;

hbi_status hbi_tokenizer_register(const hbi_tokenizer *tokenizer) {
    if (!tokenizer || !tokenizer->name || !tokenizer->encode || !tokenizer->decode) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL tokenizer or missing required fields");
    }
    /* Check for duplicates. */
    for (uint32_t i = 0u; i < g_tokenizer_registry_count; i++) {
        if (strcmp(g_tokenizer_registry[i]->name, tokenizer->name) == 0) {
            return HBI_ERR_SET(HBI_ERR_STATE, 0, "duplicate tokenizer name");
        }
    }
    if (g_tokenizer_registry_count >= HBI_TOKENIZER_REGISTRY_MAX) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "tokenizer registry full");
    }
    g_tokenizer_registry[g_tokenizer_registry_count++] = tokenizer;
    return HBI_OK;
}

const hbi_tokenizer *hbi_tokenizer_find(const char *name) {
    if (!name) {
        return NULL;
    }
    for (uint32_t i = 0u; i < g_tokenizer_registry_count; i++) {
        if (strcmp(g_tokenizer_registry[i]->name, name) == 0) {
            return g_tokenizer_registry[i];
        }
    }
    return NULL;
}

int hbi_tokenizer_count(void) {
    return (int)g_tokenizer_registry_count;
}

void hbi_tokenizer_registry_clear(void) {
    g_tokenizer_registry_count = 0u;
}

/* ── Tokenizer manager ────────────────────────────────────────────────────── */

hbi_status hbi_tokenizer_manager_create(hbi_tokenizer_manager **out, const hbi_tokenizer *tokenizer,
                                        const hbi_vocabulary *vocab, hbi_allocator *allocator) {
    if (!out || !tokenizer || !vocab || !allocator) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL argument");
    }
    hbi_tokenizer_manager *mgr = (hbi_tokenizer_manager *)hbi_alloc(
        allocator, sizeof(hbi_tokenizer_manager), 0, HBI_MEM_GENERAL);
    if (!mgr) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "tokenizer manager allocation failed");
    }
    memset(mgr, 0, sizeof(*mgr));
    mgr->tokenizer = tokenizer;
    mgr->vocabulary = vocab;
    mgr->allocator = allocator;
    mgr->encode_context = NULL;

    hbi_status st = hbi_decode_state_create(&mgr->decode_state, allocator);
    if (st != HBI_OK) {
        hbi_free(allocator, mgr);
        return st;
    }

    /* Call tokenizer init if available. */
    if (tokenizer->init) {
        st = tokenizer->init(tokenizer, vocab, allocator);
        if (st != HBI_OK) {
            hbi_decode_state_destroy(mgr->decode_state);
            hbi_free(allocator, mgr);
            return st;
        }
    }

    mgr->initialized = true;
    *out = mgr;
    return HBI_OK;
}

void hbi_tokenizer_manager_destroy(hbi_tokenizer_manager *manager) {
    if (!manager) {
        return;
    }
    /* Free encode context if present. */
    if (manager->encode_context && manager->tokenizer->free_context) {
        manager->tokenizer->free_context(manager->tokenizer, manager->encode_context);
    }
    hbi_decode_state_destroy(manager->decode_state);
    hbi_free(manager->allocator, manager);
}

hbi_status hbi_tokenizer_manager_encode(const hbi_tokenizer_manager *manager, const char *text,
                                        size_t text_len, hbi_token_sequence *out_seq) {
    if (!manager || !text || !out_seq) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL manager, text, or sequence");
    }
    if (!manager->initialized) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "manager not initialized");
    }
    uint64_t t0 = hbi_time_monotonic_ns();
    hbi_status st = manager->tokenizer->encode(manager->tokenizer, manager->vocabulary, text,
                                               text_len, out_seq);
    uint64_t t1 = hbi_time_monotonic_ns();
    if (st == HBI_OK) {
        /* Cast away const for stats update — manager is logically mutable for stats. */
        hbi_tokenizer_manager *mut = (hbi_tokenizer_manager *)(uintptr_t)manager;
        mut->stats.encode_time_ns += (t1 - t0);
        mut->stats.encode_calls++;
        mut->stats.tokens_encoded += (uint64_t)hbi_token_sequence_count(out_seq);
    }
    return st;
}

hbi_status hbi_tokenizer_manager_decode(const hbi_tokenizer_manager *manager,
                                        const hbi_token_id *tokens, uint32_t token_count,
                                        char *out_text, size_t out_capacity, size_t *out_len) {
    if (!manager || (!tokens && token_count > 0u) || !out_text || !out_len) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL argument");
    }
    if (!manager->initialized) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "manager not initialized");
    }
    uint64_t t0 = hbi_time_monotonic_ns();
    hbi_status st = manager->tokenizer->decode(manager->tokenizer, manager->vocabulary, tokens,
                                               token_count, out_text, out_capacity, out_len);
    uint64_t t1 = hbi_time_monotonic_ns();
    if (st == HBI_OK) {
        hbi_tokenizer_manager *mut = (hbi_tokenizer_manager *)(uintptr_t)manager;
        mut->stats.decode_time_ns += (t1 - t0);
        mut->stats.decode_calls++;
        mut->stats.tokens_decoded += (uint64_t)token_count;
    }
    return st;
}

hbi_status hbi_tokenizer_manager_encode_incremental(const hbi_tokenizer_manager *manager,
                                                    const char *text, size_t text_len,
                                                    hbi_token_sequence *out_seq) {
    if (!manager || !text || !out_seq) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL argument");
    }
    if (!manager->initialized) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "manager not initialized");
    }
    if (!manager->tokenizer->encode_incremental) {
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "incremental encode not supported");
    }
    hbi_tokenizer_manager *mut = (hbi_tokenizer_manager *)(uintptr_t)manager;
    return manager->tokenizer->encode_incremental(manager->tokenizer, manager->vocabulary, text,
                                                  text_len, out_seq, &mut->encode_context);
}

hbi_status hbi_tokenizer_manager_decode_incremental(const hbi_tokenizer_manager *manager,
                                                    hbi_token_id token, char *out_text,
                                                    size_t out_capacity, size_t *out_len) {
    if (!manager || !out_text || !out_len) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL argument");
    }
    if (!manager->initialized) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "manager not initialized");
    }
    if (!manager->tokenizer->decode_incremental) {
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "incremental decode not supported");
    }
    return manager->tokenizer->decode_incremental(
        manager->tokenizer, manager->vocabulary, token,
        (hbi_decode_state *)(uintptr_t)manager->decode_state, out_text, out_capacity, out_len);
}

void hbi_tokenizer_manager_reset(hbi_tokenizer_manager *manager) {
    if (!manager) {
        return;
    }
    if (manager->encode_context && manager->tokenizer->free_context) {
        manager->tokenizer->free_context(manager->tokenizer, manager->encode_context);
        manager->encode_context = NULL;
    }
    hbi_decode_state_reset(manager->decode_state);
}

const hbi_vocabulary *hbi_tokenizer_manager_vocabulary(const hbi_tokenizer_manager *manager) {
    return manager ? manager->vocabulary : NULL;
}

const hbi_tokenizer *hbi_tokenizer_manager_tokenizer(const hbi_tokenizer_manager *manager) {
    return manager ? manager->tokenizer : NULL;
}

hbi_status hbi_tokenizer_manager_get_statistics(const hbi_tokenizer_manager *manager,
                                                hbi_tokenizer_statistics *out) {
    if (!manager || !out) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL argument");
    }
    *out = manager->stats;
    return HBI_OK;
}

/* ── Module identity / self-test ──────────────────────────────────────────── */

const char *hbi_tokenizer_name(void) {
    return "tokenizer";
}

hbi_status hbi_tokenizer_selftest(void) {
    /* Validate UTF-8 round-trip for ASCII. */
    uint8_t buf[4];
    uint32_t n = hbi_utf8_encode(0x41u, buf); /* 'A' */
    if (n != 1u || buf[0] != 0x41u) {
        return HBI_ERR_INTERNAL;
    }
    uint32_t cp;
    n = hbi_utf8_decode(buf, 1u, &cp);
    if (n != 1u || cp != 0x41u) {
        return HBI_ERR_INTERNAL;
    }
    return HBI_OK;
}

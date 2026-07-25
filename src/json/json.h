/* json.h — Minimal, zero-dependency JSON parser (RFC-015, DD-035).
 *
 * Core-public header for the `json` module (layer 1: depends only on `common`
 * and `memory`). Other modules include this; external embedders use
 * <hummingbird/hummingbird.h> instead. Symbols are prefixed `hbi_` (internal, no
 * stability guarantee). See docs/architecture/03-dependency-graph.md.
 *
 * ── Purpose ──────────────────────────────────────────────────────────────────
 * The runtime must read JSON that ships alongside models: the Safetensors file
 * header, `config.json`, and (later) `tokenizer.json`. This is a small
 * recursive-descent parser producing an in-memory DOM. It is NOT a general
 * high-performance JSON library — it is the smallest correct, bounds-checked,
 * dependency-free thing that satisfies those needs.
 *
 * ── Design ───────────────────────────────────────────────────────────────────
 *  - Parses a UTF-8 buffer (not NUL-terminated required — length is explicit).
 *  - Produces a tree of hbi_json_value nodes allocated from a caller-provided
 *    hbi_allocator. Passing an arena allocator makes teardown a single reset.
 *  - Enforces a maximum nesting depth and a maximum total input size (hardening
 *    against malformed/hostile headers, cf. Colibrì's 512 MiB header cap).
 *  - Every failure returns an hbi_status and sets the thread-local error record
 *    with a byte offset. Never aborts, never calls exit() (DD-019).
 *
 * ── Ownership ────────────────────────────────────────────────────────────────
 * The returned root and every node/string within it are allocated from the
 * allocator passed to hbi_json_parse. Strings are copied out of the input, so
 * the input buffer may be freed after parsing. Free the whole DOM with
 * hbi_json_free (a no-op for arena allocators — reset the arena instead).
 *
 * ── Thread-safety ────────────────────────────────────────────────────────────
 * The parser holds no global state; concurrent parses on distinct allocators are
 * safe. The error record is thread-local as everywhere else.
 */
#ifndef HB_JSON_H
#define HB_JSON_H

#include "common/common.h"
#include "memory/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hard limits (hardening). A JSON header/config beyond these is rejected rather
 * than trusted. Callers with a legitimately larger document raise these. */
#define HBI_JSON_MAX_DEPTH 128u
#define HBI_JSON_MAX_BYTES (512u * 1024u * 1024u) /* 512 MiB, matches Colibrì */

/* ── Value kinds ─────────────────────────────────────────────────────────────*/
typedef enum hbi_json_kind {
    HBI_JSON_NULL = 0,
    HBI_JSON_BOOL,
    HBI_JSON_NUMBER, /* stored as double; integer accessor rounds/checks range */
    HBI_JSON_STRING,
    HBI_JSON_ARRAY,
    HBI_JSON_OBJECT
} hbi_json_kind;

typedef struct hbi_json_value hbi_json_value;

/* One object member: a key string and its value. */
typedef struct hbi_json_member {
    const char *key; /* NUL-terminated, owned by the DOM allocator */
    hbi_json_value *value;
} hbi_json_member;

/* A JSON DOM node. Transparent so accessors can be simple; prefer the accessor
 * functions below over reaching into the union. */
struct hbi_json_value {
    hbi_json_kind kind;
    union {
        bool boolean;
        double number;
        const char *string; /* NUL-terminated, owned by the DOM allocator */
        struct {
            hbi_json_value **items;
            size_t count;
        } array;
        struct {
            hbi_json_member *members;
            size_t count;
        } object;
    } u;
};

/* ── Parse / free ─────────────────────────────────────────────────────────── */

/* Parse `len` bytes of UTF-8 JSON at `text` into a DOM rooted at *out_root, using
 * `allocator` for every node/string. `allocator` NULL uses the system allocator.
 * Trailing whitespace after the top-level value is allowed; trailing non-space
 * content is an error.
 *
 * Fails HBI_ERR_INVALID_ARG (NULL text/out, len 0), HBI_ERR_UNSUPPORTED
 * (len > HBI_JSON_MAX_BYTES), HBI_ERR_CORRUPT (malformed JSON or depth exceeded —
 * the error record carries a byte offset), or HBI_ERR_OOM. On failure *out_root
 * is NULL and any partial allocations are released for the system allocator (for
 * an arena, reset it). */
hbi_status hbi_json_parse(const char *text, size_t len, hbi_allocator *allocator,
                          hbi_json_value **out_root);

/* Free a DOM previously returned by hbi_json_parse, using the SAME allocator.
 * NULL-safe. A no-op in practice for arena allocators (free() is a no-op there);
 * reset the arena instead. */
void hbi_json_free(hbi_json_value *root, hbi_allocator *allocator);

/* ── Accessors ───────────────────────────────────────────────────────────────
 * All are NULL-safe and kind-checked: a mismatched kind returns the failure
 * sentinel (NULL / false / 0) without setting the error record — callers test
 * the return, or use hbi_json_is_* first. */

hbi_json_kind hbi_json_kind_of(const hbi_json_value *v); /* HBI_JSON_NULL if v==NULL */

bool hbi_json_is_object(const hbi_json_value *v);
bool hbi_json_is_array(const hbi_json_value *v);
bool hbi_json_is_string(const hbi_json_value *v);
bool hbi_json_is_number(const hbi_json_value *v);
bool hbi_json_is_bool(const hbi_json_value *v);

/* Object: look up a member by key. NULL if v is not an object or key is absent. */
const hbi_json_value *hbi_json_object_get(const hbi_json_value *v, const char *key);
size_t hbi_json_object_count(const hbi_json_value *v);
/* Iterate members by index (0..count). NULL out params are ignored. */
bool hbi_json_object_member_at(const hbi_json_value *v, size_t index, const char **out_key,
                               const hbi_json_value **out_value);

/* Array: element count and indexed access (NULL if out of range / not array). */
size_t hbi_json_array_count(const hbi_json_value *v);
const hbi_json_value *hbi_json_array_at(const hbi_json_value *v, size_t index);

/* Scalars. The string accessor returns a borrowed pointer into the DOM (valid
 * until the DOM is freed). */
const char *hbi_json_as_string(const hbi_json_value *v); /* NULL if not a string */
double hbi_json_as_number(const hbi_json_value *v);      /* 0.0 if not a number */
bool hbi_json_as_bool(const hbi_json_value *v);          /* false if not a bool */

/* Convert a NUMBER to int64. Fails HBI_ERR_INVALID_ARG (not a number/NULL) or
 * HBI_ERR_CORRUPT (non-integral or out of int64 range). */
hbi_status hbi_json_as_int(const hbi_json_value *v, int64_t *out);

/* ── Module identity / self-test ─────────────────────────────────────────── */
const char *hbi_json_name(void);
hbi_status hbi_json_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_JSON_H */

/* json.c — minimal recursive-descent JSON parser (RFC-015, DD-035).
 *
 * Grammar (RFC 8259 subset, sufficient for safetensors headers + config.json):
 *   value   := object | array | string | number | 'true' | 'false' | 'null'
 *   object  := '{' (string ':' value (',' string ':' value)*)? '}'
 *   array   := '[' (value (',' value)*)? ']'
 *   string  := '"' char* '"'   (with \" \\ \/ \b \f \n \r \t \uXXXX escapes)
 *   number  := JSON number (parsed with strtod)
 *
 * Correctness-first: every step is bounds-checked against the explicit length,
 * depth is capped, and each failure sets the thread-local error record with the
 * byte offset. No global state; no exit().
 */
#include "json/json_internal.h"

#include <stdlib.h>
#include <string.h>

/* ── Small allocation helpers ─────────────────────────────────────────────── */

static void *jalloc(hbi_json_parser *p, size_t n) {
    void *ptr = hbi_alloc(p->alloc, n, 0, HBI_MEM_GENERAL);
    if (!ptr) {
        HBI_ERR_SET(HBI_ERR_OOM, 0, "json: allocation failed");
    }
    return ptr;
}

static hbi_json_value *new_value(hbi_json_parser *p, hbi_json_kind kind) {
    hbi_json_value *v = (hbi_json_value *)jalloc(p, sizeof(*v));
    if (!v) {
        return NULL;
    }
    memset(v, 0, sizeof(*v));
    v->kind = kind;
    return v;
}

/* ── Cursor primitives ────────────────────────────────────────────────────── */

static bool at_end(const hbi_json_parser *p) {
    return p->pos >= p->len;
}

static char peek(const hbi_json_parser *p) {
    return at_end(p) ? '\0' : p->buf[p->pos];
}

static void skip_ws(hbi_json_parser *p) {
    while (!at_end(p)) {
        char c = p->buf[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else {
            break;
        }
    }
}

static hbi_status err_at(hbi_json_parser *p, const char *what) {
    return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "json: %s at byte %zu", what, p->pos);
}

/* Forward decl for recursion. */
static hbi_status parse_value(hbi_json_parser *p, hbi_json_value **out);

/* ── String parsing ───────────────────────────────────────────────────────── */

/* Append the UTF-8 encoding of `cp` to buf[*len], growing nothing (caller sized
 * the buffer to at least the raw escape length, which always >= encoded length).
 */
static void utf8_emit(char *buf, size_t *len, uint32_t cp) {
    if (cp <= 0x7F) {
        buf[(*len)++] = (char)cp;
    } else if (cp <= 0x7FF) {
        buf[(*len)++] = (char)(0xC0 | (cp >> 6));
        buf[(*len)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        buf[(*len)++] = (char)(0xE0 | (cp >> 12));
        buf[(*len)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[(*len)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        buf[(*len)++] = (char)(0xF0 | (cp >> 18));
        buf[(*len)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[(*len)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[(*len)++] = (char)(0x80 | (cp & 0x3F));
    }
}

static bool parse_hex4(hbi_json_parser *p, uint32_t *out) {
    if (p->pos + 4 > p->len) {
        return false;
    }
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p->buf[p->pos++];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v |= (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            v |= (uint32_t)(c - 'A' + 10);
        } else {
            return false;
        }
    }
    *out = v;
    return true;
}

/* Parse a JSON string starting at the opening quote. Writes a freshly allocated
 * NUL-terminated copy to *out_str. */
static hbi_status parse_string_raw(hbi_json_parser *p, const char **out_str) {
    if (peek(p) != '"') {
        return err_at(p, "expected '\"'");
    }
    p->pos++; /* consume opening quote */

    /* The decoded string is never longer than the raw span between quotes, so we
     * can size the output buffer to the remaining input length once. */
    size_t start = p->pos;
    size_t cap = p->len - start + 1;
    char *buf = (char *)jalloc(p, cap);
    if (!buf) {
        return HBI_ERR_OOM;
    }
    size_t out_len = 0;

    while (!at_end(p)) {
        char c = p->buf[p->pos++];
        if (c == '"') {
            buf[out_len] = '\0';
            *out_str = buf;
            return HBI_OK;
        }
        if (c == '\\') {
            if (at_end(p)) {
                break;
            }
            char e = p->buf[p->pos++];
            switch (e) {
            case '"':
                buf[out_len++] = '"';
                break;
            case '\\':
                buf[out_len++] = '\\';
                break;
            case '/':
                buf[out_len++] = '/';
                break;
            case 'b':
                buf[out_len++] = '\b';
                break;
            case 'f':
                buf[out_len++] = '\f';
                break;
            case 'n':
                buf[out_len++] = '\n';
                break;
            case 'r':
                buf[out_len++] = '\r';
                break;
            case 't':
                buf[out_len++] = '\t';
                break;
            case 'u': {
                uint32_t cp = 0;
                if (!parse_hex4(p, &cp)) {
                    hbi_free(p->alloc, buf);
                    return err_at(p, "bad \\u escape");
                }
                /* Surrogate pair. */
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (p->pos + 2 <= p->len && p->buf[p->pos] == '\\' &&
                        p->buf[p->pos + 1] == 'u') {
                        p->pos += 2;
                        uint32_t lo = 0;
                        if (!parse_hex4(p, &lo)) {
                            hbi_free(p->alloc, buf);
                            return err_at(p, "bad low surrogate");
                        }
                        if (lo < 0xDC00 || lo > 0xDFFF) {
                            hbi_free(p->alloc, buf);
                            return err_at(p, "invalid low surrogate");
                        }
                        cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
                    } else {
                        hbi_free(p->alloc, buf);
                        return err_at(p, "unpaired high surrogate");
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    hbi_free(p->alloc, buf);
                    return err_at(p, "unpaired low surrogate");
                }
                utf8_emit(buf, &out_len, cp);
                break;
            }
            default:
                hbi_free(p->alloc, buf);
                return err_at(p, "invalid escape");
            }
        } else if ((unsigned char)c < 0x20) {
            hbi_free(p->alloc, buf);
            return err_at(p, "unescaped control character");
        } else {
            buf[out_len++] = c;
        }
    }
    hbi_free(p->alloc, buf);
    return err_at(p, "unterminated string");
}

/* ── Number parsing ───────────────────────────────────────────────────────── */

static hbi_status parse_number(hbi_json_parser *p, hbi_json_value **out) {
    size_t start = p->pos;
    /* Scan a maximal JSON-number span. strtod is stricter than we need but the
     * pre-scan bounds it to the buffer and rejects stray characters after. */
    while (!at_end(p)) {
        char c = p->buf[p->pos];
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
            p->pos++;
        } else {
            break;
        }
    }
    size_t n = p->pos - start;
    if (n == 0) {
        return err_at(p, "expected number");
    }
    /* Copy into a small stack/temp buffer to guarantee NUL-termination for
     * strtod (the input is not NUL-terminated at the token boundary). */
    char tmp[64];
    if (n >= sizeof(tmp)) {
        return err_at(p, "number too long");
    }
    memcpy(tmp, p->buf + start, n);
    tmp[n] = '\0';
    char *end = NULL;
    double d = strtod(tmp, &end);
    if (!end || *end != '\0') {
        p->pos = start;
        return err_at(p, "malformed number");
    }
    hbi_json_value *v = new_value(p, HBI_JSON_NUMBER);
    if (!v) {
        return HBI_ERR_OOM;
    }
    v->u.number = d;
    *out = v;
    return HBI_OK;
}

/* ── Literal parsing (true/false/null) ────────────────────────────────────── */

static hbi_status parse_literal(hbi_json_parser *p, const char *lit, hbi_json_kind kind,
                                bool boolean, hbi_json_value **out) {
    size_t n = strlen(lit);
    if (p->pos + n > p->len || memcmp(p->buf + p->pos, lit, n) != 0) {
        return err_at(p, "invalid literal");
    }
    p->pos += n;
    hbi_json_value *v = new_value(p, kind);
    if (!v) {
        return HBI_ERR_OOM;
    }
    if (kind == HBI_JSON_BOOL) {
        v->u.boolean = boolean;
    }
    *out = v;
    return HBI_OK;
}

/* ── Array / object parsing ───────────────────────────────────────────────── */

static hbi_status parse_array(hbi_json_parser *p, hbi_json_value **out) {
    p->pos++; /* consume '[' */
    hbi_json_value *v = new_value(p, HBI_JSON_ARRAY);
    if (!v) {
        return HBI_ERR_OOM;
    }
    v->u.array.items = NULL;
    v->u.array.count = 0;

    skip_ws(p);
    if (peek(p) == ']') {
        p->pos++;
        *out = v;
        return HBI_OK;
    }

    size_t cap = 8;
    hbi_json_value **items = (hbi_json_value **)jalloc(p, cap * sizeof(*items));
    if (!items) {
        hbi_free(p->alloc, v);
        return HBI_ERR_OOM;
    }
    v->u.array.items = items;

    for (;;) {
        hbi_json_value *elem = NULL;
        hbi_status st = parse_value(p, &elem);
        if (st != HBI_OK) {
            hbi_json_free(v, p->alloc);
            return st;
        }
        if (v->u.array.count == cap) {
            size_t ncap = cap * 2;
            hbi_json_value **ni = (hbi_json_value **)hbi_realloc(
                p->alloc, v->u.array.items, ncap * sizeof(*items), 0, HBI_MEM_GENERAL);
            if (!ni) {
                hbi_json_free(elem, p->alloc);
                hbi_json_free(v, p->alloc);
                return HBI_ERR_SET(HBI_ERR_OOM, 0, "json: array grow failed");
            }
            v->u.array.items = ni;
            cap = ncap;
        }
        v->u.array.items[v->u.array.count++] = elem;

        skip_ws(p);
        char c = peek(p);
        if (c == ',') {
            p->pos++;
            skip_ws(p);
            continue;
        }
        if (c == ']') {
            p->pos++;
            break;
        }
        hbi_json_free(v, p->alloc);
        return err_at(p, "expected ',' or ']'");
    }

    *out = v;
    return HBI_OK;
}

static hbi_status parse_object(hbi_json_parser *p, hbi_json_value **out) {
    p->pos++; /* consume '{' */
    hbi_json_value *v = new_value(p, HBI_JSON_OBJECT);
    if (!v) {
        return HBI_ERR_OOM;
    }
    v->u.object.members = NULL;
    v->u.object.count = 0;

    skip_ws(p);
    if (peek(p) == '}') {
        p->pos++;
        *out = v;
        return HBI_OK;
    }

    size_t cap = 8;
    hbi_json_member *members = (hbi_json_member *)jalloc(p, cap * sizeof(*members));
    if (!members) {
        hbi_free(p->alloc, v);
        return HBI_ERR_OOM;
    }
    v->u.object.members = members;

    for (;;) {
        skip_ws(p);
        const char *key = NULL;
        hbi_status st = parse_string_raw(p, &key);
        if (st != HBI_OK) {
            hbi_json_free(v, p->alloc);
            return st;
        }
        skip_ws(p);
        if (peek(p) != ':') {
            hbi_free(p->alloc, (void *)(uintptr_t)key);
            hbi_json_free(v, p->alloc);
            return err_at(p, "expected ':'");
        }
        p->pos++;
        skip_ws(p);

        hbi_json_value *val = NULL;
        st = parse_value(p, &val);
        if (st != HBI_OK) {
            hbi_free(p->alloc, (void *)(uintptr_t)key);
            hbi_json_free(v, p->alloc);
            return st;
        }

        if (v->u.object.count == cap) {
            size_t ncap = cap * 2;
            hbi_json_member *nm = (hbi_json_member *)hbi_realloc(
                p->alloc, v->u.object.members, ncap * sizeof(*members), 0, HBI_MEM_GENERAL);
            if (!nm) {
                hbi_json_free(val, p->alloc);
                hbi_free(p->alloc, (void *)(uintptr_t)key);
                hbi_json_free(v, p->alloc);
                return HBI_ERR_SET(HBI_ERR_OOM, 0, "json: object grow failed");
            }
            v->u.object.members = nm;
            cap = ncap;
        }
        v->u.object.members[v->u.object.count].key = key;
        v->u.object.members[v->u.object.count].value = val;
        v->u.object.count++;

        skip_ws(p);
        char c = peek(p);
        if (c == ',') {
            p->pos++;
            continue;
        }
        if (c == '}') {
            p->pos++;
            break;
        }
        hbi_json_free(v, p->alloc);
        return err_at(p, "expected ',' or '}'");
    }

    *out = v;
    return HBI_OK;
}

/* ── Value dispatch ───────────────────────────────────────────────────────── */

static hbi_status parse_value(hbi_json_parser *p, hbi_json_value **out) {
    if (p->depth >= HBI_JSON_MAX_DEPTH) {
        return err_at(p, "maximum nesting depth exceeded");
    }
    skip_ws(p);
    if (at_end(p)) {
        return err_at(p, "unexpected end of input");
    }
    char c = peek(p);
    hbi_status st;
    switch (c) {
    case '{':
        p->depth++;
        st = parse_object(p, out);
        p->depth--;
        return st;
    case '[':
        p->depth++;
        st = parse_array(p, out);
        p->depth--;
        return st;
    case '"': {
        const char *s = NULL;
        st = parse_string_raw(p, &s);
        if (st != HBI_OK) {
            return st;
        }
        hbi_json_value *v = new_value(p, HBI_JSON_STRING);
        if (!v) {
            hbi_free(p->alloc, (void *)(uintptr_t)s);
            return HBI_ERR_OOM;
        }
        v->u.string = s;
        *out = v;
        return HBI_OK;
    }
    case 't':
        return parse_literal(p, "true", HBI_JSON_BOOL, true, out);
    case 'f':
        return parse_literal(p, "false", HBI_JSON_BOOL, false, out);
    case 'n':
        return parse_literal(p, "null", HBI_JSON_NULL, false, out);
    default:
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parse_number(p, out);
        }
        return err_at(p, "unexpected character");
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

hbi_status hbi_json_parse(const char *text, size_t len, hbi_allocator *allocator,
                          hbi_json_value **out_root) {
    if (!text || !out_root || len == 0) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "json_parse: NULL/empty input");
    }
    if (len > HBI_JSON_MAX_BYTES) {
        return HBI_ERR_SETF(HBI_ERR_UNSUPPORTED, 0, "json_parse: input %zu exceeds cap %u", len,
                            (unsigned)HBI_JSON_MAX_BYTES);
    }
    *out_root = NULL;

    hbi_json_parser p = {
        .buf = text,
        .len = len,
        .pos = 0,
        .depth = 0,
        .alloc = allocator ? allocator : hbi_allocator_system(),
    };

    hbi_json_value *root = NULL;
    hbi_status st = parse_value(&p, &root);
    if (st != HBI_OK) {
        return st;
    }
    skip_ws(&p);
    if (!at_end(&p)) {
        hbi_json_free(root, p.alloc);
        return err_at(&p, "trailing content after top-level value");
    }
    *out_root = root;
    return HBI_OK;
}

/* Free a DOM-owned string/key. The DOM stores them as const (they are logically
 * immutable to readers) but the DOM allocator owns them, so freeing is valid.
 * Route through uintptr_t to drop const without tripping -Wcast-qual. */
static void free_owned_str(hbi_allocator *a, const char *s) {
    hbi_free(a, (void *)(uintptr_t)s);
}

void hbi_json_free(hbi_json_value *root, hbi_allocator *allocator) {
    /* The DOM is a tree of independent allocations. For the system allocator we
     * walk and free; for an arena, free() is a no-op and the caller resets. To
     * keep this simple and allocator-agnostic we recursively free children then
     * the node. Strings/keys are separate allocations. */
    if (!root) {
        return;
    }
    hbi_allocator *a = allocator ? allocator : hbi_allocator_system();
    switch (root->kind) {
    case HBI_JSON_STRING:
        free_owned_str(a, root->u.string);
        break;
    case HBI_JSON_ARRAY:
        for (size_t i = 0; i < root->u.array.count; i++) {
            hbi_json_free(root->u.array.items[i], a);
        }
        hbi_free(a, root->u.array.items);
        break;
    case HBI_JSON_OBJECT:
        for (size_t i = 0; i < root->u.object.count; i++) {
            free_owned_str(a, root->u.object.members[i].key);
            hbi_json_free(root->u.object.members[i].value, a);
        }
        hbi_free(a, root->u.object.members);
        break;
    default:
        break;
    }
    hbi_free(a, root);
}

/* ── Accessors ───────────────────────────────────────────────────────────── */

hbi_json_kind hbi_json_kind_of(const hbi_json_value *v) {
    return v ? v->kind : HBI_JSON_NULL;
}

bool hbi_json_is_object(const hbi_json_value *v) {
    return v && v->kind == HBI_JSON_OBJECT;
}
bool hbi_json_is_array(const hbi_json_value *v) {
    return v && v->kind == HBI_JSON_ARRAY;
}
bool hbi_json_is_string(const hbi_json_value *v) {
    return v && v->kind == HBI_JSON_STRING;
}
bool hbi_json_is_number(const hbi_json_value *v) {
    return v && v->kind == HBI_JSON_NUMBER;
}
bool hbi_json_is_bool(const hbi_json_value *v) {
    return v && v->kind == HBI_JSON_BOOL;
}

const hbi_json_value *hbi_json_object_get(const hbi_json_value *v, const char *key) {
    if (!hbi_json_is_object(v) || !key) {
        return NULL;
    }
    for (size_t i = 0; i < v->u.object.count; i++) {
        if (strcmp(v->u.object.members[i].key, key) == 0) {
            return v->u.object.members[i].value;
        }
    }
    return NULL;
}

size_t hbi_json_object_count(const hbi_json_value *v) {
    return hbi_json_is_object(v) ? v->u.object.count : 0;
}

bool hbi_json_object_member_at(const hbi_json_value *v, size_t index, const char **out_key,
                               const hbi_json_value **out_value) {
    if (!hbi_json_is_object(v) || index >= v->u.object.count) {
        return false;
    }
    if (out_key) {
        *out_key = v->u.object.members[index].key;
    }
    if (out_value) {
        *out_value = v->u.object.members[index].value;
    }
    return true;
}

size_t hbi_json_array_count(const hbi_json_value *v) {
    return hbi_json_is_array(v) ? v->u.array.count : 0;
}

const hbi_json_value *hbi_json_array_at(const hbi_json_value *v, size_t index) {
    if (!hbi_json_is_array(v) || index >= v->u.array.count) {
        return NULL;
    }
    return v->u.array.items[index];
}

const char *hbi_json_as_string(const hbi_json_value *v) {
    return hbi_json_is_string(v) ? v->u.string : NULL;
}

double hbi_json_as_number(const hbi_json_value *v) {
    return hbi_json_is_number(v) ? v->u.number : 0.0;
}

bool hbi_json_as_bool(const hbi_json_value *v) {
    return hbi_json_is_bool(v) ? v->u.boolean : false;
}

hbi_status hbi_json_as_int(const hbi_json_value *v, int64_t *out) {
    if (!hbi_json_is_number(v) || !out) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "json_as_int: not a number / NULL out");
    }
    double d = v->u.number;
    /* Reject non-integral and out-of-range values. 2^63 as double is exactly
     * representable; compare against it to bound the range safely. */
    if (d != (double)(int64_t)d) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "json_as_int: %g is not integral", d);
    }
    if (d < -9223372036854775808.0 || d >= 9223372036854775808.0) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "json_as_int: %g out of int64 range", d);
    }
    *out = (int64_t)d;
    return HBI_OK;
}

/* ── Module identity / self-test ─────────────────────────────────────────── */

const char *hbi_json_name(void) {
    return "json";
}

hbi_status hbi_json_selftest(void) {
    const char *doc = "{\"a\":1,\"b\":[true,null,\"x\"]}";
    hbi_json_value *root = NULL;
    hbi_status st = hbi_json_parse(doc, strlen(doc), NULL, &root);
    if (st != HBI_OK) {
        return st;
    }
    hbi_status result = HBI_OK;
    if (!hbi_json_is_object(root) || hbi_json_object_count(root) != 2) {
        result = HBI_ERR_INTERNAL;
    }
    hbi_json_free(root, NULL);
    return result;
}

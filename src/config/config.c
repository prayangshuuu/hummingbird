/* config.c — typed, validated, precedence-aware configuration (DD-006, DD-024).
 *
 * Storage: one hbi_config_entry per schema descriptor, in schema order. String
 * values are heap-copied and owned by the entry; every other type is stored
 * inline in a small tagged union. Lookups are linear over the entry array — a
 * config has a few dozen keys at most, so a hash map would be more code than it
 * is worth and would allocate on every build.
 */
#include "config/config_internal.h"

#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Value storage ─────────────────────────────────────────────────────────── */

typedef struct hbi_config_entry {
    const hbi_config_desc *desc; /* borrowed from the schema */
    hbi_config_source source;    /* where the current value came from */
    union {
        bool b;
        int64_t i;
        uint64_t u;
        char *s; /* owned heap copy (NULL == empty string) */
    } val;
} hbi_config_entry;

struct hbi_config {
    const hbi_config_desc *schema; /* borrowed; terminated by key == NULL */
    hbi_config_entry *entries;
    size_t count;
};

/* ── Enum spellings ────────────────────────────────────────────────────────── */

const char *hbi_config_type_str(hbi_config_type type) {
    switch (type) {
    case HBI_CFG_BOOL:
        return "bool";
    case HBI_CFG_INT:
        return "int";
    case HBI_CFG_UINT:
        return "uint";
    case HBI_CFG_STRING:
        return "string";
    }
    return "unknown";
}

const char *hbi_config_source_str(hbi_config_source source) {
    switch (source) {
    case HBI_CFG_SRC_DEFAULT:
        return "default";
    case HBI_CFG_SRC_FILE:
        return "file";
    case HBI_CFG_SRC_ENV:
        return "env";
    case HBI_CFG_SRC_SET:
        return "set";
    }
    return "unknown";
}

/* ── Small helpers ─────────────────────────────────────────────────────────── */

/* Duplicate a NUL-terminated string with the platform allocator. Returns NULL
 * only on OOM (an empty input yields a valid empty string). */
static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *out = (char *)hbi_aligned_alloc(sizeof(void *), n);
    if (out != NULL) {
        memcpy(out, s, n);
    }
    return out;
}

static void entry_free_string(hbi_config_entry *e) {
    if (e->desc->type == HBI_CFG_STRING && e->val.s != NULL) {
        hbi_aligned_free(e->val.s);
        e->val.s = NULL;
    }
}

static hbi_config_entry *find_entry(const hbi_config *cfg, const char *key) {
    for (size_t i = 0; i < cfg->count; ++i) {
        if (strcmp(cfg->entries[i].desc->key, key) == 0) {
            return &cfg->entries[i];
        }
    }
    return NULL;
}

/* Trim leading/trailing ASCII whitespace in place, returning the new start. */
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        ++s;
    }
    char *end = s + strlen(s);
    while (end > s) {
        char c = end[-1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            --end;
        } else {
            break;
        }
    }
    *end = '\0';
    return s;
}

/* ── Value assignment with validation ────────────────────────────────────────
 * Assign a parsed value to an entry, tagging it with `source`. Precedence is
 * enforced by the caller (it decides whether to call); this only validates and
 * stores. Bounds violations return HBI_ERR_CORRUPT; OOM returns HBI_ERR_OOM. */

static hbi_status set_bool(hbi_config_entry *e, bool v, hbi_config_source src) {
    e->val.b = v;
    e->source = src;
    return HBI_OK;
}

static hbi_status set_int(hbi_config_entry *e, int64_t v, hbi_config_source src) {
    const hbi_config_desc *d = e->desc;
    if (!(d->min_int == 0 && d->max_int == 0) && (v < d->min_int || v > d->max_int)) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "config '%s': %lld out of range [%lld,%lld]",
                            d->key, (long long)v, (long long)d->min_int, (long long)d->max_int);
    }
    e->val.i = v;
    e->source = src;
    return HBI_OK;
}

static hbi_status set_uint(hbi_config_entry *e, uint64_t v, hbi_config_source src) {
    const hbi_config_desc *d = e->desc;
    if (!(d->min_uint == 0 && d->max_uint == 0) && (v < d->min_uint || v > d->max_uint)) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "config '%s': %llu out of range [%llu,%llu]",
                            d->key, (unsigned long long)v, (unsigned long long)d->min_uint,
                            (unsigned long long)d->max_uint);
    }
    e->val.u = v;
    e->source = src;
    return HBI_OK;
}

static hbi_status set_string(hbi_config_entry *e, const char *v, hbi_config_source src) {
    char *copy = dup_str(v);
    if (copy == NULL) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "config: string copy failed");
    }
    entry_free_string(e);
    e->val.s = copy;
    e->source = src;
    return HBI_OK;
}

/* Parse a textual value per the entry's type and assign it. Used by the file,
 * env, and key/value paths (all of which deliver strings). */
static hbi_status assign_from_text(hbi_config_entry *e, const char *text, hbi_config_source src) {
    const hbi_config_desc *d = e->desc;
    switch (d->type) {
    case HBI_CFG_BOOL: {
        bool v;
        if (strcmp(text, "1") == 0 || strcmp(text, "true") == 0 || strcmp(text, "on") == 0 ||
            strcmp(text, "yes") == 0) {
            v = true;
        } else if (strcmp(text, "0") == 0 || strcmp(text, "false") == 0 ||
                   strcmp(text, "off") == 0 || strcmp(text, "no") == 0) {
            v = false;
        } else {
            return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "config '%s': '%s' is not a bool", d->key,
                                text);
        }
        return set_bool(e, v, src);
    }
    case HBI_CFG_INT: {
        char *end = NULL;
        long long v = strtoll(text, &end, 10);
        if (end == text || *end != '\0') {
            return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "config '%s': '%s' is not an int", d->key,
                                text);
        }
        return set_int(e, (int64_t)v, src);
    }
    case HBI_CFG_UINT: {
        char *end = NULL;
        /* Reject a leading '-': strtoull would silently wrap it. */
        if (text[0] == '-') {
            return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "config '%s': '%s' is negative", d->key, text);
        }
        unsigned long long v = strtoull(text, &end, 10);
        if (end == text || *end != '\0') {
            return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "config '%s': '%s' is not a uint", d->key,
                                text);
        }
        return set_uint(e, (uint64_t)v, src);
    }
    case HBI_CFG_STRING:
        return set_string(e, text, src);
    }
    return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "config: unknown type");
}

/* ── Schema validation & lifecycle ───────────────────────────────────────────*/

static hbi_status validate_schema(const hbi_config_desc *schema, size_t *out_count) {
    size_t n = 0;
    for (const hbi_config_desc *d = schema; d->key != NULL; ++d) {
        /* Duplicate-key check against everything before it. */
        for (const hbi_config_desc *p = schema; p != d; ++p) {
            if (strcmp(p->key, d->key) == 0) {
                return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "config schema: duplicate key '%s'",
                                    d->key);
            }
        }
        if (d->type == HBI_CFG_INT && d->min_int > d->max_int) {
            return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "config schema: '%s' min_int > max_int",
                                d->key);
        }
        if (d->type == HBI_CFG_UINT && d->min_uint > d->max_uint) {
            return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "config schema: '%s' min_uint > max_uint",
                                d->key);
        }
        if (d->type == HBI_CFG_STRING && d->def_string == NULL) {
            return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "config schema: '%s' string default is NULL",
                                d->key);
        }
        ++n;
    }
    *out_count = n;
    return HBI_OK;
}

hbi_status hbi_config_create(hbi_config **out, const hbi_config_desc *schema) {
    if (out == NULL || schema == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "config_create: NULL argument");
    }
    *out = NULL;

    size_t count = 0;
    hbi_status st = validate_schema(schema, &count);
    if (st != HBI_OK) {
        return st;
    }
    if (count == 0) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "config_create: empty schema");
    }

    hbi_config *cfg = (hbi_config *)hbi_aligned_alloc(sizeof(void *), sizeof(*cfg));
    if (cfg == NULL) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "config_create: object alloc failed");
    }
    cfg->schema = schema;
    cfg->count = count;
    cfg->entries =
        (hbi_config_entry *)hbi_aligned_alloc(sizeof(void *), count * sizeof(hbi_config_entry));
    if (cfg->entries == NULL) {
        hbi_aligned_free(cfg);
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "config_create: entry alloc failed");
    }

    /* Initialize every entry to its default (source = DEFAULT). */
    for (size_t i = 0; i < count; ++i) {
        hbi_config_entry *e = &cfg->entries[i];
        e->desc = &schema[i];
        e->source = HBI_CFG_SRC_DEFAULT;
        switch (schema[i].type) {
        case HBI_CFG_BOOL:
            e->val.b = schema[i].def_bool;
            break;
        case HBI_CFG_INT:
            e->val.i = schema[i].def_int;
            break;
        case HBI_CFG_UINT:
            e->val.u = schema[i].def_uint;
            break;
        case HBI_CFG_STRING:
            e->val.s = dup_str(schema[i].def_string);
            if (e->val.s == NULL) {
                /* Unwind the strings already allocated. */
                for (size_t j = 0; j < i; ++j) {
                    entry_free_string(&cfg->entries[j]);
                }
                hbi_aligned_free(cfg->entries);
                hbi_aligned_free(cfg);
                return HBI_ERR_SET(HBI_ERR_OOM, 0, "config_create: default string alloc failed");
            }
            break;
        }
    }

    *out = cfg;
    return HBI_OK;
}

void hbi_config_destroy(hbi_config *cfg) {
    if (cfg == NULL) {
        return;
    }
    for (size_t i = 0; i < cfg->count; ++i) {
        entry_free_string(&cfg->entries[i]);
    }
    hbi_aligned_free(cfg->entries);
    hbi_aligned_free(cfg);
}

/* ── Precedence-aware application ─────────────────────────────────────────────
 * Only assign if `src` >= the entry's current source, so a higher-precedence
 * value already in place is never clobbered by a lower-precedence load. */
static hbi_status apply_text(hbi_config *cfg, const char *key, const char *text,
                             hbi_config_source src) {
    hbi_config_entry *e = find_entry(cfg, key);
    if (e == NULL) {
        return HBI_ERR_SETF(HBI_ERR_NOT_FOUND, 0, "config: unknown key '%s'", key);
    }
    if (src < e->source) {
        return HBI_OK; /* keep the higher-precedence value */
    }
    return assign_from_text(e, text, src);
}

/* ── File loading ────────────────────────────────────────────────────────────*/

hbi_status hbi_config_load_file(hbi_config *cfg, const char *path) {
    if (cfg == NULL || path == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "config_load_file: NULL argument");
    }

    hbi_file *f = NULL;
    hbi_status st = hbi_file_open(&f, path, HBI_FILE_READ);
    if (st != HBI_OK) {
        return st; /* error already recorded by platform */
    }

    uint64_t size64 = 0;
    st = hbi_file_size(f, &size64);
    if (st != HBI_OK) {
        hbi_file_close(f);
        return st;
    }
    /* A config file is tiny; cap defensively so a bad path can't ask for GBs. */
    if (size64 > (16u * 1024u * 1024u)) {
        hbi_file_close(f);
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "config file '%s' too large (%llu bytes)", path,
                            (unsigned long long)size64);
    }

    size_t size = (size_t)size64;
    char *buf = (char *)hbi_aligned_alloc(sizeof(void *), size + 1);
    if (buf == NULL) {
        hbi_file_close(f);
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "config_load_file: buffer alloc failed");
    }

    size_t got = 0;
    st = hbi_file_read(f, buf, size, &got);
    hbi_file_close(f);
    if (st != HBI_OK) {
        hbi_aligned_free(buf);
        return st;
    }
    buf[got] = '\0';

    /* Parse line by line. We mutate `buf` in place (splitting on '\n' and '='). */
    hbi_status result = HBI_OK;
    int lineno = 0;
    char *cursor = buf;
    while (cursor != NULL && *cursor != '\0') {
        ++lineno;
        char *nl = strchr(cursor, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        char *line = trim(cursor);
        cursor = (nl != NULL) ? nl + 1 : NULL;

        if (line[0] == '\0' || line[0] == '#') {
            continue; /* blank or comment */
        }
        char *eq = strchr(line, '=');
        if (eq == NULL) {
            result =
                HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "config '%s' line %d: missing '='", path, lineno);
            break;
        }
        *eq = '\0';
        char *key = trim(line);
        char *value = trim(eq + 1);
        hbi_status one = apply_text(cfg, key, value, HBI_CFG_SRC_FILE);
        if (one != HBI_OK) {
            result = one; /* error record already set by apply_text */
            break;
        }
    }

    hbi_aligned_free(buf);
    return result;
}

/* ── Environment loading ─────────────────────────────────────────────────────*/

hbi_status hbi_config_load_env(hbi_config *cfg) {
    if (cfg == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "config_load_env: NULL config");
    }
    for (size_t i = 0; i < cfg->count; ++i) {
        const hbi_config_desc *d = cfg->entries[i].desc;
        if (d->env == NULL) {
            continue;
        }
        const char *raw = getenv(d->env);
        if (raw == NULL || raw[0] == '\0') {
            continue;
        }
        if (HBI_CFG_SRC_ENV < cfg->entries[i].source) {
            continue; /* a higher-precedence value is already set */
        }
        hbi_status st = assign_from_text(&cfg->entries[i], raw, HBI_CFG_SRC_ENV);
        if (st != HBI_OK) {
            return st;
        }
    }
    return HBI_OK;
}

hbi_status hbi_config_apply_kv(hbi_config *cfg, const char *key, const char *value) {
    if (cfg == NULL || key == NULL || value == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "config_apply_kv: NULL argument");
    }
    return apply_text(cfg, key, value, HBI_CFG_SRC_SET);
}

/* ── Typed getters ───────────────────────────────────────────────────────────*/

/* Look up an entry expecting a given type. Records an error and returns NULL on
 * unknown key or type mismatch. */
static const hbi_config_entry *get_typed(const hbi_config *cfg, const char *key,
                                         hbi_config_type type) {
    if (cfg == NULL || key == NULL) {
        HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "config get: NULL argument");
        return NULL;
    }
    const hbi_config_entry *e = find_entry(cfg, key);
    if (e == NULL) {
        HBI_ERR_SETF(HBI_ERR_NOT_FOUND, 0, "config get: unknown key '%s'", key);
        return NULL;
    }
    if (e->desc->type != type) {
        HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "config get '%s': type is %s, asked for %s", key,
                     hbi_config_type_str(e->desc->type), hbi_config_type_str(type));
        return NULL;
    }
    return e;
}

bool hbi_config_get_bool(const hbi_config *cfg, const char *key, bool fallback) {
    const hbi_config_entry *e = get_typed(cfg, key, HBI_CFG_BOOL);
    return e ? e->val.b : fallback;
}

int64_t hbi_config_get_int(const hbi_config *cfg, const char *key, int64_t fallback) {
    const hbi_config_entry *e = get_typed(cfg, key, HBI_CFG_INT);
    return e ? e->val.i : fallback;
}

uint64_t hbi_config_get_uint(const hbi_config *cfg, const char *key, uint64_t fallback) {
    const hbi_config_entry *e = get_typed(cfg, key, HBI_CFG_UINT);
    return e ? e->val.u : fallback;
}

const char *hbi_config_get_string(const hbi_config *cfg, const char *key, const char *fallback) {
    const hbi_config_entry *e = get_typed(cfg, key, HBI_CFG_STRING);
    return e ? e->val.s : fallback;
}

/* ── Typed setters ───────────────────────────────────────────────────────────*/

static hbi_config_entry *set_typed(hbi_config *cfg, const char *key, hbi_config_type type,
                                   hbi_status *st) {
    if (cfg == NULL || key == NULL) {
        *st = HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "config set: NULL argument");
        return NULL;
    }
    hbi_config_entry *e = find_entry(cfg, key);
    if (e == NULL) {
        *st = HBI_ERR_SETF(HBI_ERR_NOT_FOUND, 0, "config set: unknown key '%s'", key);
        return NULL;
    }
    if (e->desc->type != type) {
        *st = HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "config set '%s': type is %s, gave %s", key,
                           hbi_config_type_str(e->desc->type), hbi_config_type_str(type));
        return NULL;
    }
    *st = HBI_OK;
    return e;
}

hbi_status hbi_config_set_bool(hbi_config *cfg, const char *key, bool value) {
    hbi_status st;
    hbi_config_entry *e = set_typed(cfg, key, HBI_CFG_BOOL, &st);
    return e ? set_bool(e, value, HBI_CFG_SRC_SET) : st;
}

hbi_status hbi_config_set_int(hbi_config *cfg, const char *key, int64_t value) {
    hbi_status st;
    hbi_config_entry *e = set_typed(cfg, key, HBI_CFG_INT, &st);
    return e ? set_int(e, value, HBI_CFG_SRC_SET) : st;
}

hbi_status hbi_config_set_uint(hbi_config *cfg, const char *key, uint64_t value) {
    hbi_status st;
    hbi_config_entry *e = set_typed(cfg, key, HBI_CFG_UINT, &st);
    return e ? set_uint(e, value, HBI_CFG_SRC_SET) : st;
}

hbi_status hbi_config_set_string(hbi_config *cfg, const char *key, const char *value) {
    if (value == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "config_set_string: NULL value");
    }
    hbi_status st;
    hbi_config_entry *e = set_typed(cfg, key, HBI_CFG_STRING, &st);
    return e ? set_string(e, value, HBI_CFG_SRC_SET) : st;
}

/* ── Introspection ───────────────────────────────────────────────────────────*/

size_t hbi_config_count(const hbi_config *cfg) {
    return cfg ? cfg->count : 0;
}

const hbi_config_desc *hbi_config_desc_at(const hbi_config *cfg, size_t index) {
    if (cfg == NULL || index >= cfg->count) {
        return NULL;
    }
    return cfg->entries[index].desc;
}

hbi_config_source hbi_config_source_of(const hbi_config *cfg, const char *key) {
    const hbi_config_entry *e = (cfg && key) ? find_entry(cfg, key) : NULL;
    return e ? e->source : HBI_CFG_SRC_DEFAULT;
}

int hbi_config_format_entry(const hbi_config *cfg, size_t index, char *buf, size_t cap) {
    if (cfg == NULL || index >= cfg->count) {
        if (buf != NULL && cap > 0) {
            buf[0] = '\0';
        }
        return -1;
    }
    const hbi_config_entry *e = &cfg->entries[index];
    const hbi_config_desc *d = e->desc;
    const char *src = hbi_config_source_str(e->source);

    switch (d->type) {
    case HBI_CFG_BOOL:
        return snprintf(buf, cap, "%s = %s (%s)", d->key, e->val.b ? "true" : "false", src);
    case HBI_CFG_INT:
        return snprintf(buf, cap, "%s = %lld (%s)", d->key, (long long)e->val.i, src);
    case HBI_CFG_UINT:
        return snprintf(buf, cap, "%s = %llu (%s)", d->key, (unsigned long long)e->val.u, src);
    case HBI_CFG_STRING:
        return snprintf(buf, cap, "%s = \"%s\" (%s)", d->key, e->val.s ? e->val.s : "", src);
    }
    return -1;
}

/* ── Module identity / self-test ─────────────────────────────────────────────*/

const char *hbi_config_name(void) {
    return "config";
}

hbi_status hbi_config_selftest(void) {
    /* Enum spellings must be total (never "unknown" for a valid value). */
    if (strcmp(hbi_config_type_str(HBI_CFG_STRING), "unknown") == 0) {
        return HBI_ERR_INTERNAL;
    }
    if (strcmp(hbi_config_source_str(HBI_CFG_SRC_ENV), "unknown") == 0) {
        return HBI_ERR_INTERNAL;
    }
    return HBI_OK;
}

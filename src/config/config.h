/* config.h — Typed, validated configuration (DD-006, DD-024).
 *
 * Core-public header for the `config` module (layer 2). Replaces Colibrì's ~80
 * ad-hoc getenv() calls with a typed, validated, introspectable store. Symbols
 * are prefixed `hbi_` (internal, no stability guarantee); external embedders use
 * <hummingbird/hummingbird.h>.
 *
 * Model:
 *   - A config is a fixed set of typed ENTRIES described by a static SCHEMA
 *     (key, type, default, bounds, help text). The schema is data, so `plan` /
 *     `doctor` frontends can print every knob and its origin without hard-coding.
 *   - Values arrive from four SOURCES with increasing precedence (DD-024):
 *         default  <  file  <  environment  <  programmatic
 *     Loading a lower source never overrides a value already set by a higher
 *     one; each entry remembers which source last set it (for introspection).
 *   - Types are the minimum a foundation needs: bool, int64, uint64, string.
 *     Richer types (enums, sizes with K/M/G suffixes) layer on later without an
 *     ABI change — they are still stored as one of these.
 *
 * This module performs FILE I/O (reading a config file) — permitted by the
 * dependency rules (docs/architecture/03-dependency-graph.md). It depends only
 * on `common` and `platform`; it does NOT depend on `logging` (that would be a
 * forbidden sideways layer-2 edge — validation failures are reported by return
 * code + the common error record, which the caller may log).
 *
 * Thread-safety: a config object is NOT internally locked. Build and populate it
 * during single-threaded startup, then treat it as read-only (safe to read
 * concurrently). Mutating it after threads start is the caller's hazard.
 */
#ifndef HB_CONFIG_H
#define HB_CONFIG_H

#include "common/common.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Types & sources ─────────────────────────────────────────────────────── */

typedef enum hbi_config_type {
    HBI_CFG_BOOL = 0,
    HBI_CFG_INT,  /* int64_t  */
    HBI_CFG_UINT, /* uint64_t */
    HBI_CFG_STRING
} hbi_config_type;

/* Where an entry's current value came from. Ordered by precedence: a later
 * source may overwrite an earlier one, never the reverse. */
typedef enum hbi_config_source {
    HBI_CFG_SRC_DEFAULT = 0,
    HBI_CFG_SRC_FILE = 1,
    HBI_CFG_SRC_ENV = 2,
    HBI_CFG_SRC_SET = 3 /* programmatic hbi_config_set_* */
} hbi_config_source;

const char *hbi_config_type_str(hbi_config_type type);
const char *hbi_config_source_str(hbi_config_source source);

/* ── Schema ──────────────────────────────────────────────────────────────────
 * A schema is a static array of descriptors terminated by a {0} entry (key ==
 * NULL). Bounds apply to INT/UINT only (min/max inclusive); leave both 0 to mean
 * "unbounded". `def_*` supplies the default per type. `env` is the environment
 * variable name (e.g. "HB_LOG_LEVEL"); NULL means the key is not env-loadable. */
typedef struct hbi_config_desc {
    const char *key; /* canonical dotted key, e.g. "runtime.threads" */
    hbi_config_type type;
    const char *env;  /* env var name, or NULL */
    const char *help; /* one-line description for introspection */
    bool def_bool;
    int64_t def_int;
    uint64_t def_uint;
    const char *def_string; /* borrowed; must outlive the config (static) */
    int64_t min_int;        /* INT bounds (inclusive); 0/0 == unbounded */
    int64_t max_int;
    uint64_t min_uint; /* UINT bounds (inclusive); 0/0 == unbounded */
    uint64_t max_uint;
} hbi_config_desc;

/* ── Object lifecycle ────────────────────────────────────────────────────────
 * Create a config bound to `schema` (borrowed; must outlive the config). Every
 * entry starts at its default with source DEFAULT. Returns HBI_ERR_INVALID_ARG
 * on NULL/empty schema, HBI_ERR_OOM on allocation failure, HBI_ERR_CORRUPT if
 * the schema is malformed (duplicate key, bad bounds). */
typedef struct hbi_config hbi_config;

hbi_status hbi_config_create(hbi_config **out, const hbi_config_desc *schema);
void hbi_config_destroy(hbi_config *cfg); /* NULL is a no-op */

/* ── Loading (precedence-aware) ──────────────────────────────────────────────
 * Each loader only sets entries whose new source >= current source, so calling
 * them in order default→file→env→set yields the documented precedence. */

/* Parse a "key=value" file (one per line; blank lines and '#' comments ignored;
 * surrounding whitespace trimmed; keys not in the schema are reported). Returns
 * HBI_ERR_NOT_FOUND if the file is absent, HBI_ERR_IO on a read failure, or
 * HBI_ERR_CORRUPT on a malformed line or a value that fails type/bounds
 * validation (the error record names the line). */
hbi_status hbi_config_load_file(hbi_config *cfg, const char *path);

/* Read every schema entry that declares an `env` name from the environment.
 * Missing env vars are skipped. A present-but-invalid value is an error. */
hbi_status hbi_config_load_env(hbi_config *cfg);

/* Apply a single "key=value" override (as from a CLI flag). Highest precedence
 * short of a typed setter. */
hbi_status hbi_config_apply_kv(hbi_config *cfg, const char *key, const char *value);

/* ── Typed getters ───────────────────────────────────────────────────────────
 * Return the current value; on unknown key or type mismatch they return the
 * supplied `fallback` and record an error (so misuse is visible without
 * branching every call site). */
bool hbi_config_get_bool(const hbi_config *cfg, const char *key, bool fallback);
int64_t hbi_config_get_int(const hbi_config *cfg, const char *key, int64_t fallback);
uint64_t hbi_config_get_uint(const hbi_config *cfg, const char *key, uint64_t fallback);
const char *hbi_config_get_string(const hbi_config *cfg, const char *key, const char *fallback);

/* ── Typed setters (programmatic, highest precedence) ────────────────────────
 * Validate against the schema (type + bounds) and mark the source as SET.
 * Returns HBI_ERR_NOT_FOUND (unknown key), HBI_ERR_INVALID_ARG (type mismatch),
 * or HBI_ERR_CORRUPT (out of bounds). */
hbi_status hbi_config_set_bool(hbi_config *cfg, const char *key, bool value);
hbi_status hbi_config_set_int(hbi_config *cfg, const char *key, int64_t value);
hbi_status hbi_config_set_uint(hbi_config *cfg, const char *key, uint64_t value);
hbi_status hbi_config_set_string(hbi_config *cfg, const char *key, const char *value);

/* ── Introspection ───────────────────────────────────────────────────────────
 * Enough for a `plan`/`doctor` command to dump the full effective config and
 * where each value came from. */
size_t hbi_config_count(const hbi_config *cfg);
const hbi_config_desc *hbi_config_desc_at(const hbi_config *cfg, size_t index);
hbi_config_source hbi_config_source_of(const hbi_config *cfg, const char *key);

/* Format entry `index` as "key = value (source)" into buf (always NUL-terminated
 * when cap > 0). Returns snprintf-style length, or -1 if index is out of range. */
int hbi_config_format_entry(const hbi_config *cfg, size_t index, char *buf, size_t cap);

/* ── Module identity / self-test ─────────────────────────────────────────── */
const char *hbi_config_name(void);
hbi_status hbi_config_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_CONFIG_H */

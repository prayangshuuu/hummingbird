/* model.c — Model Loader Framework implementation (RFC-011).
 *
 * Format-independent loading pipeline, tensor manifest, metadata management,
 * and format-handler registry. See model.h for the full API contract.
 */
#include "model/model_internal.h"

#include "platform/platform.h"

#include <string.h>

/* ── Format string helpers ───────────────────────────────────────────────── */

static const char *const k_format_names[] = {
    "unknown",
    "gguf",
    "safetensors",
};

const char *hbi_model_format_str(hbi_model_format fmt) {
    if (fmt >= 0 && fmt < HBI_MODEL_FORMAT_COUNT) {
        return k_format_names[fmt];
    }
    return "unknown";
}

/* ── Format handler registry ─────────────────────────────────────────────── */

static const hbi_format_handler *g_handlers[HBI_FORMAT_HANDLER_MAX];
static int g_handler_count = 0;

hbi_status hbi_format_handler_register(const hbi_format_handler *handler) {
    if (!handler || !handler->name || !handler->detect || !handler->parse_metadata) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0,
                           "format handler: NULL handler or missing required fields");
    }
    if (g_handler_count >= HBI_FORMAT_HANDLER_MAX) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "format handler registry is full");
    }
    /* Check for duplicate format. */
    for (int i = 0; i < g_handler_count; ++i) {
        if (g_handlers[i]->format == handler->format) {
            return HBI_ERR_SETF(HBI_ERR_STATE, 0, "format handler already registered for %s",
                                hbi_model_format_str(handler->format));
        }
    }
    g_handlers[g_handler_count++] = handler;
    return HBI_OK;
}

hbi_model_format hbi_format_handler_detect(const char *path) {
    if (!path) {
        return HBI_MODEL_FORMAT_UNKNOWN;
    }
    for (int i = 0; i < g_handler_count; ++i) {
        if (g_handlers[i]->detect(path)) {
            return g_handlers[i]->format;
        }
    }
    return HBI_MODEL_FORMAT_UNKNOWN;
}

const hbi_format_handler *hbi_format_handler_find(hbi_model_format fmt) {
    for (int i = 0; i < g_handler_count; ++i) {
        if (g_handlers[i]->format == fmt) {
            return g_handlers[i];
        }
    }
    return NULL;
}

int hbi_format_handler_count(void) {
    return g_handler_count;
}

void hbi_format_handler_registry_clear(void) {
    g_handler_count = 0;
    memset(g_handlers, 0, sizeof(g_handlers));
}

/* ── Manifest ────────────────────────────────────────────────────────────── */

hbi_status hbi_model_manifest_create(hbi_allocator *allocator, hbi_model_manifest **out) {
    if (!allocator || !out) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "manifest create: NULL arg");
    }
    hbi_model_manifest *m =
        (hbi_model_manifest *)hbi_alloc(allocator, sizeof(hbi_model_manifest), 8, HBI_MEM_GENERAL);
    if (!m) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "manifest create: alloc failed");
    }
    m->allocator = allocator;
    m->count = 0;
    m->capacity = HBI_MANIFEST_INITIAL_CAP;
    m->entries = (hbi_tensor_entry *)hbi_alloc(allocator, sizeof(hbi_tensor_entry) * m->capacity, 8,
                                               HBI_MEM_GENERAL);
    if (!m->entries) {
        hbi_free(allocator, m);
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "manifest create: entries alloc failed");
    }
    *out = m;
    return HBI_OK;
}

void hbi_model_manifest_destroy(hbi_model_manifest *manifest) {
    if (!manifest) {
        return;
    }
    if (manifest->entries) {
        hbi_free(manifest->allocator, manifest->entries);
    }
    hbi_allocator *a = manifest->allocator;
    hbi_free(a, manifest);
}

static hbi_status manifest_grow(hbi_model_manifest *m) {
    uint32_t new_cap = m->capacity * 2;
    hbi_tensor_entry *new_entries = (hbi_tensor_entry *)hbi_realloc(
        m->allocator, m->entries, sizeof(hbi_tensor_entry) * new_cap, 8, HBI_MEM_GENERAL);
    if (!new_entries) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "manifest grow: realloc failed");
    }
    m->entries = new_entries;
    m->capacity = new_cap;
    return HBI_OK;
}

hbi_status hbi_model_manifest_add(hbi_model_manifest *manifest, const hbi_tensor_entry *entry) {
    if (!manifest || !entry) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "manifest add: NULL arg");
    }
    if (entry->name[0] == '\0') {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "manifest add: empty tensor name");
    }
    if (entry->byte_size == 0) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "manifest add: zero byte_size for '%s'",
                            entry->name);
    }
    if (!hbi_dtype_is_valid(entry->dtype)) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "manifest add: invalid dtype for '%s'",
                            entry->name);
    }
    /* Duplicate check. */
    for (uint32_t i = 0; i < manifest->count; ++i) {
        if (strcmp(manifest->entries[i].name, entry->name) == 0) {
            return HBI_ERR_SETF(HBI_ERR_STATE, 0, "manifest add: duplicate tensor '%s'",
                                entry->name);
        }
    }
    /* Grow if needed. */
    if (manifest->count >= manifest->capacity) {
        hbi_status st = manifest_grow(manifest);
        if (st != HBI_OK) {
            return st;
        }
    }
    manifest->entries[manifest->count] = *entry;
    manifest->count++;
    return HBI_OK;
}

uint32_t hbi_model_manifest_count(const hbi_model_manifest *manifest) {
    return manifest ? manifest->count : 0;
}

const hbi_tensor_entry *hbi_model_manifest_entry(const hbi_model_manifest *manifest,
                                                 uint32_t index) {
    if (!manifest || index >= manifest->count) {
        return NULL;
    }
    return &manifest->entries[index];
}

const hbi_tensor_entry *hbi_model_manifest_find(const hbi_model_manifest *manifest,
                                                const char *name) {
    if (!manifest || !name) {
        return NULL;
    }
    for (uint32_t i = 0; i < manifest->count; ++i) {
        if (strcmp(manifest->entries[i].name, name) == 0) {
            return &manifest->entries[i];
        }
    }
    return NULL;
}

hbi_status hbi_model_manifest_validate(const hbi_model_manifest *manifest) {
    if (!manifest) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "manifest validate: NULL");
    }
    for (uint32_t i = 0; i < manifest->count; ++i) {
        const hbi_tensor_entry *e = &manifest->entries[i];
        if (e->name[0] == '\0') {
            return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0,
                                "manifest validate: entry %u has empty name", i);
        }
        if (!hbi_dtype_is_valid(e->dtype)) {
            return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "manifest validate: '%s' has invalid dtype",
                                e->name);
        }
        if (e->byte_size == 0) {
            return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0,
                                "manifest validate: '%s' has zero byte_size", e->name);
        }
        /* Validate shape. */
        hbi_status st = hbi_shape_validate(&e->shape);
        if (st != HBI_OK) {
            return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "manifest validate: '%s' has invalid shape",
                                e->name);
        }
    }
    return HBI_OK;
}

/* ── Metadata ────────────────────────────────────────────────────────────── */

hbi_status hbi_model_metadata_create(hbi_allocator *allocator, hbi_model_metadata **out) {
    if (!allocator || !out) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "metadata create: NULL arg");
    }
    hbi_model_metadata *md =
        (hbi_model_metadata *)hbi_alloc(allocator, sizeof(hbi_model_metadata), 8, HBI_MEM_GENERAL);
    if (!md) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "metadata create: alloc failed");
    }
    md->allocator = allocator;
    md->count = 0;
    md->capacity = HBI_METADATA_INITIAL_CAP;
    md->pairs = (hbi_metadata_pair *)hbi_alloc(allocator, sizeof(hbi_metadata_pair) * md->capacity,
                                               8, HBI_MEM_GENERAL);
    if (!md->pairs) {
        hbi_free(allocator, md);
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "metadata create: pairs alloc failed");
    }
    *out = md;
    return HBI_OK;
}

void hbi_model_metadata_destroy(hbi_model_metadata *metadata) {
    if (!metadata) {
        return;
    }
    if (metadata->pairs) {
        hbi_free(metadata->allocator, metadata->pairs);
    }
    hbi_allocator *a = metadata->allocator;
    hbi_free(a, metadata);
}

hbi_status hbi_model_metadata_set(hbi_model_metadata *metadata, const char *key,
                                  const char *value) {
    if (!metadata || !key || !value) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "metadata set: NULL arg");
    }
    if (strlen(key) >= HBI_METADATA_KEY_MAX) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "metadata set: key too long");
    }
    if (strlen(value) >= HBI_METADATA_VALUE_MAX) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "metadata set: value too long");
    }
    /* Overwrite if key exists. */
    for (uint32_t i = 0; i < metadata->count; ++i) {
        if (strcmp(metadata->pairs[i].key, key) == 0) {
            strncpy(metadata->pairs[i].value, value, HBI_METADATA_VALUE_MAX - 1);
            metadata->pairs[i].value[HBI_METADATA_VALUE_MAX - 1] = '\0';
            return HBI_OK;
        }
    }
    /* Add new pair. */
    if (metadata->count >= metadata->capacity) {
        uint32_t new_cap = metadata->capacity * 2;
        hbi_metadata_pair *new_pairs = (hbi_metadata_pair *)hbi_realloc(
            metadata->allocator, metadata->pairs, sizeof(hbi_metadata_pair) * new_cap, 8,
            HBI_MEM_GENERAL);
        if (!new_pairs) {
            return HBI_ERR_SET(HBI_ERR_OOM, 0, "metadata set: realloc failed");
        }
        metadata->pairs = new_pairs;
        metadata->capacity = new_cap;
    }
    strncpy(metadata->pairs[metadata->count].key, key, HBI_METADATA_KEY_MAX - 1);
    metadata->pairs[metadata->count].key[HBI_METADATA_KEY_MAX - 1] = '\0';
    strncpy(metadata->pairs[metadata->count].value, value, HBI_METADATA_VALUE_MAX - 1);
    metadata->pairs[metadata->count].value[HBI_METADATA_VALUE_MAX - 1] = '\0';
    metadata->count++;
    return HBI_OK;
}

const char *hbi_model_metadata_get(const hbi_model_metadata *metadata, const char *key) {
    if (!metadata || !key) {
        return NULL;
    }
    for (uint32_t i = 0; i < metadata->count; ++i) {
        if (strcmp(metadata->pairs[i].key, key) == 0) {
            return metadata->pairs[i].value;
        }
    }
    return NULL;
}

uint32_t hbi_model_metadata_count(const hbi_model_metadata *metadata) {
    return metadata ? metadata->count : 0;
}

/* ── Load session ────────────────────────────────────────────────────────── */

static hbi_status load_session_create(hbi_allocator *allocator, hbi_load_session **out) {
    hbi_load_session *s =
        (hbi_load_session *)hbi_alloc(allocator, sizeof(hbi_load_session), 8, HBI_MEM_GENERAL);
    if (!s) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "load session: alloc failed");
    }
    memset(s, 0, sizeof(*s));
    s->allocator = allocator;
    s->state = HBI_LOAD_STATE_INIT;

    hbi_status st = hbi_model_manifest_create(allocator, &s->manifest);
    if (st != HBI_OK) {
        hbi_free(allocator, s);
        return st;
    }
    st = hbi_model_metadata_create(allocator, &s->metadata);
    if (st != HBI_OK) {
        hbi_model_manifest_destroy(s->manifest);
        hbi_free(allocator, s);
        return st;
    }
    *out = s;
    return HBI_OK;
}

void hbi_model_load_session_destroy(hbi_load_session *session) {
    if (!session) {
        return;
    }
    hbi_model_manifest_destroy(session->manifest);
    hbi_model_metadata_destroy(session->metadata);
    hbi_allocator *a = session->allocator;
    hbi_free(a, session);
}

const hbi_model_manifest *hbi_load_session_manifest(const hbi_load_session *session) {
    return session ? session->manifest : NULL;
}

const hbi_model_metadata *hbi_load_session_metadata(const hbi_load_session *session) {
    return session ? session->metadata : NULL;
}

const hbi_load_statistics *hbi_load_session_statistics(const hbi_load_session *session) {
    return session ? &session->stats : NULL;
}

hbi_model_format hbi_load_session_format(const hbi_load_session *session) {
    return session ? session->detected_format : HBI_MODEL_FORMAT_UNKNOWN;
}

/* ── Load pipeline ───────────────────────────────────────────────────────── */

hbi_status hbi_model_load(const hbi_load_options *options, hbi_allocator *allocator,
                          hbi_load_session **out_session) {
    if (!options || !allocator || !out_session) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "model load: NULL arg");
    }
    *out_session = NULL;

    if (!options->model_path || options->model_path[0] == '\0') {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "model load: empty model_path");
    }

    uint64_t t_start = hbi_time_monotonic_ns();

    /* 1. Create session. */
    hbi_load_session *session = NULL;
    hbi_status st = load_session_create(allocator, &session);
    if (st != HBI_OK) {
        return st;
    }
    session->options = *options;

    /* 2. Detect format. */
    hbi_model_format fmt = options->format_hint;
    if (fmt == HBI_MODEL_FORMAT_UNKNOWN) {
        fmt = hbi_format_handler_detect(options->model_path);
    }
    if (fmt == HBI_MODEL_FORMAT_UNKNOWN) {
        session->state = HBI_LOAD_STATE_FAILED;
        hbi_model_load_session_destroy(session);
        return HBI_ERR_SETF(HBI_ERR_UNSUPPORTED, 0, "model load: could not detect format for '%s'",
                            options->model_path);
    }
    session->detected_format = fmt;

    /* 3. Find handler. */
    const hbi_format_handler *handler = hbi_format_handler_find(fmt);
    if (!handler) {
        session->state = HBI_LOAD_STATE_FAILED;
        hbi_model_load_session_destroy(session);
        return HBI_ERR_SETF(HBI_ERR_NOT_FOUND, 0, "model load: no handler for format '%s'",
                            hbi_model_format_str(fmt));
    }
    session->handler = handler;

    /* 4. Parse metadata. */
    uint64_t t_parse_start = hbi_time_monotonic_ns();
    st = handler->parse_metadata(options->model_path, allocator, session->manifest,
                                 session->metadata);
    session->stats.metadata_parse_time_ns = hbi_time_monotonic_ns() - t_parse_start;

    if (st != HBI_OK) {
        session->state = HBI_LOAD_STATE_FAILED;
        hbi_model_load_session_destroy(session);
        return st;
    }
    session->state = HBI_LOAD_STATE_PARSED;

    /* 5. Validate manifest. */
    uint64_t t_val_start = hbi_time_monotonic_ns();
    st = hbi_model_manifest_validate(session->manifest);
    session->stats.validation_time_ns = hbi_time_monotonic_ns() - t_val_start;

    if (st != HBI_OK) {
        session->stats.validation_errors++;
        if (options->flags & HBI_LOAD_STRICT) {
            session->state = HBI_LOAD_STATE_FAILED;
            hbi_model_load_session_destroy(session);
            return st;
        }
        /* Non-strict: log as warning, continue. */
        session->stats.validation_warnings++;
    }
    session->state = HBI_LOAD_STATE_VALIDATED;

    /* 6. Compute statistics. */
    session->stats.tensors_registered = hbi_model_manifest_count(session->manifest);
    uint64_t total_bytes = 0;
    for (uint32_t i = 0; i < session->stats.tensors_registered; ++i) {
        const hbi_tensor_entry *e = hbi_model_manifest_entry(session->manifest, i);
        total_bytes += e->byte_size;
    }
    session->stats.bytes_indexed = total_bytes;

    /* 7. Finalize. */
    session->state = HBI_LOAD_STATE_COMPLETE;
    session->stats.total_load_time_ns = hbi_time_monotonic_ns() - t_start;

    *out_session = session;
    return HBI_OK;
}

/* ── Module identity / self-test ─────────────────────────────────────────── */

const char *hbi_model_name(void) {
    return "model";
}

hbi_status hbi_model_selftest(void) {
    /* Verify format string table. */
    if (strcmp(hbi_model_format_str(HBI_MODEL_FORMAT_GGUF), "gguf") != 0) {
        return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "model selftest: format string mismatch");
    }
    return HBI_OK;
}

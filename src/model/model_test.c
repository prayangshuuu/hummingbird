/* model_test.c — comprehensive tests for the Model Loader Framework (RFC-011). */
#include "memory/memory.h"
#include "model/model.h"
#include "model/model_internal.h"

#include <stdio.h>
#include <string.h>

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

/* ── Mock format handler for testing the pipeline ────────────────────────── */

static bool mock_detect(const char *path) {
    /* Match any path ending in ".mock" */
    const char *ext = strrchr(path, '.');
    return ext && strcmp(ext, ".mock") == 0;
}

static hbi_status mock_parse_metadata(const char *path, hbi_allocator *allocator,
                                      hbi_model_manifest *manifest, hbi_model_metadata *metadata) {
    HB_UNUSED(path);
    HB_UNUSED(allocator);

    /* Add two fake tensor entries. */
    hbi_tensor_entry e1;
    memset(&e1, 0, sizeof(e1));
    strncpy(e1.name, "model.embed_tokens.weight", HBI_TENSOR_NAME_MAX - 1);
    e1.shape.rank = 2;
    e1.shape.dims[0] = 128256;
    e1.shape.dims[1] = 4096;
    e1.dtype = HBI_DTYPE_FP32;
    e1.file_offset = 1024;
    e1.byte_size = 128256u * 4096u * 4u;
    e1.required_alignment = 64;
    e1.shard_index = 0;
    e1.residency = HBI_RESIDENCY_RESIDENT;

    hbi_status st = hbi_model_manifest_add(manifest, &e1);
    if (st != HBI_OK) {
        return st;
    }

    hbi_tensor_entry e2;
    memset(&e2, 0, sizeof(e2));
    strncpy(e2.name, "model.layers.0.self_attn.q_proj.weight", HBI_TENSOR_NAME_MAX - 1);
    e2.shape.rank = 2;
    e2.shape.dims[0] = 4096;
    e2.shape.dims[1] = 4096;
    e2.dtype = HBI_DTYPE_FP16;
    e2.file_offset = 2101248;
    e2.byte_size = 4096u * 4096u * 2u;
    e2.required_alignment = 64;
    e2.shard_index = 0;
    e2.residency = HBI_RESIDENCY_STREAMABLE;

    st = hbi_model_manifest_add(manifest, &e2);
    if (st != HBI_OK) {
        return st;
    }

    /* Add metadata. */
    st = hbi_model_metadata_set(metadata, "architecture", "llama");
    if (st != HBI_OK) {
        return st;
    }
    st = hbi_model_metadata_set(metadata, "hidden_size", "4096");
    if (st != HBI_OK) {
        return st;
    }

    return HBI_OK;
}

static hbi_status mock_read_tensor_data(const char *path, const hbi_tensor_entry *entry, void *buf,
                                        size_t buf_size) {
    HB_UNUSED(path);
    HB_UNUSED(entry);
    HB_UNUSED(buf);
    HB_UNUSED(buf_size);
    return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "mock: read not implemented");
}

static const hbi_format_handler g_mock_handler = {
    .name = "mock",
    .format = HBI_MODEL_FORMAT_GGUF, /* reuse an enum value for the mock */
    .detect = mock_detect,
    .parse_metadata = mock_parse_metadata,
    .read_tensor_data = mock_read_tensor_data,
};

/* ── Tests ───────────────────────────────────────────────────────────────── */

static int test_manifest_lifecycle(void) {
    hbi_model_manifest *m = NULL;
    hbi_status st = hbi_model_manifest_create(hbi_allocator_system(), &m);
    ASSERT(st == HBI_OK, "manifest create");
    ASSERT(hbi_model_manifest_count(m) == 0, "manifest starts empty");

    /* Add an entry. */
    hbi_tensor_entry e;
    memset(&e, 0, sizeof(e));
    strncpy(e.name, "test.weight", HBI_TENSOR_NAME_MAX - 1);
    e.shape.rank = 1;
    e.shape.dims[0] = 100;
    e.dtype = HBI_DTYPE_FP32;
    e.byte_size = 400;
    e.required_alignment = 4;

    st = hbi_model_manifest_add(m, &e);
    ASSERT(st == HBI_OK, "manifest add");
    ASSERT(hbi_model_manifest_count(m) == 1, "manifest count == 1");

    /* Find by name. */
    const hbi_tensor_entry *found = hbi_model_manifest_find(m, "test.weight");
    ASSERT(found != NULL, "manifest find");
    ASSERT(strcmp(found->name, "test.weight") == 0, "manifest find name match");
    ASSERT(found->byte_size == 400, "manifest find byte_size");

    /* Find by index. */
    const hbi_tensor_entry *by_idx = hbi_model_manifest_entry(m, 0);
    ASSERT(by_idx != NULL, "manifest entry by index");
    ASSERT(by_idx == found, "manifest entry ptrs match");

    /* Not found. */
    ASSERT(hbi_model_manifest_find(m, "nonexistent") == NULL, "find nonexistent");
    ASSERT(hbi_model_manifest_entry(m, 999) == NULL, "entry out of range");

    hbi_model_manifest_destroy(m);
    return 0;
}

static int test_manifest_duplicate_rejected(void) {
    hbi_model_manifest *m = NULL;
    hbi_model_manifest_create(hbi_allocator_system(), &m);

    hbi_tensor_entry e;
    memset(&e, 0, sizeof(e));
    strncpy(e.name, "dup.weight", HBI_TENSOR_NAME_MAX - 1);
    e.shape.rank = 1;
    e.shape.dims[0] = 10;
    e.dtype = HBI_DTYPE_FP32;
    e.byte_size = 40;

    ASSERT(hbi_model_manifest_add(m, &e) == HBI_OK, "first add ok");
    ASSERT(hbi_model_manifest_add(m, &e) == HBI_ERR_STATE, "duplicate rejected");
    ASSERT(hbi_model_manifest_count(m) == 1, "count stays 1");

    hbi_model_manifest_destroy(m);
    return 0;
}

static int test_manifest_validation(void) {
    hbi_model_manifest *m = NULL;
    hbi_model_manifest_create(hbi_allocator_system(), &m);

    /* Empty manifest is valid. */
    ASSERT(hbi_model_manifest_validate(m) == HBI_OK, "empty manifest valid");

    /* Add a valid entry. */
    hbi_tensor_entry e;
    memset(&e, 0, sizeof(e));
    strncpy(e.name, "valid", HBI_TENSOR_NAME_MAX - 1);
    e.shape.rank = 1;
    e.shape.dims[0] = 10;
    e.dtype = HBI_DTYPE_FP32;
    e.byte_size = 40;
    hbi_model_manifest_add(m, &e);
    ASSERT(hbi_model_manifest_validate(m) == HBI_OK, "valid manifest passes");

    hbi_model_manifest_destroy(m);

    /* Invalid entry: empty name. */
    hbi_model_manifest_create(hbi_allocator_system(), &m);
    hbi_tensor_entry bad;
    memset(&bad, 0, sizeof(bad));
    bad.dtype = HBI_DTYPE_FP32;
    bad.byte_size = 10;
    bad.shape.rank = 1;
    bad.shape.dims[0] = 1;
    ASSERT(hbi_model_manifest_add(m, &bad) == HBI_ERR_INVALID_ARG, "empty name rejected on add");
    hbi_model_manifest_destroy(m);

    /* Invalid entry: zero byte_size. */
    hbi_model_manifest_create(hbi_allocator_system(), &m);
    memset(&bad, 0, sizeof(bad));
    strncpy(bad.name, "zero_size", HBI_TENSOR_NAME_MAX - 1);
    bad.dtype = HBI_DTYPE_FP32;
    bad.byte_size = 0;
    bad.shape.rank = 1;
    bad.shape.dims[0] = 1;
    ASSERT(hbi_model_manifest_add(m, &bad) == HBI_ERR_INVALID_ARG,
           "zero byte_size rejected on add");
    hbi_model_manifest_destroy(m);

    return 0;
}

static int test_metadata_lifecycle(void) {
    hbi_model_metadata *md = NULL;
    hbi_status st = hbi_model_metadata_create(hbi_allocator_system(), &md);
    ASSERT(st == HBI_OK, "metadata create");
    ASSERT(hbi_model_metadata_count(md) == 0, "metadata starts empty");

    st = hbi_model_metadata_set(md, "arch", "llama");
    ASSERT(st == HBI_OK, "metadata set");
    ASSERT(hbi_model_metadata_count(md) == 1, "metadata count == 1");

    const char *val = hbi_model_metadata_get(md, "arch");
    ASSERT(val != NULL, "metadata get");
    ASSERT(strcmp(val, "llama") == 0, "metadata value match");

    /* Overwrite. */
    st = hbi_model_metadata_set(md, "arch", "gpt2");
    ASSERT(st == HBI_OK, "metadata overwrite");
    ASSERT(hbi_model_metadata_count(md) == 1, "count stays 1 after overwrite");
    val = hbi_model_metadata_get(md, "arch");
    ASSERT(strcmp(val, "gpt2") == 0, "overwritten value match");

    /* Not found. */
    ASSERT(hbi_model_metadata_get(md, "nonexistent") == NULL, "get nonexistent");

    hbi_model_metadata_destroy(md);
    return 0;
}

static int test_format_handler_registry(void) {
    hbi_format_handler_registry_clear();
    ASSERT(hbi_format_handler_count() == 0, "registry starts empty");

    hbi_status st = hbi_format_handler_register(&g_mock_handler);
    ASSERT(st == HBI_OK, "register mock handler");
    ASSERT(hbi_format_handler_count() == 1, "count == 1");

    /* Duplicate format rejected. */
    st = hbi_format_handler_register(&g_mock_handler);
    ASSERT(st == HBI_ERR_STATE, "duplicate format rejected");

    /* Detect. */
    hbi_model_format fmt = hbi_format_handler_detect("model.mock");
    ASSERT(fmt == HBI_MODEL_FORMAT_GGUF, "detect mock format");

    fmt = hbi_format_handler_detect("model.unknown");
    ASSERT(fmt == HBI_MODEL_FORMAT_UNKNOWN, "detect unknown format");

    /* Find. */
    const hbi_format_handler *h = hbi_format_handler_find(HBI_MODEL_FORMAT_GGUF);
    ASSERT(h != NULL, "find handler");
    ASSERT(h == &g_mock_handler, "find returns mock handler");

    ASSERT(hbi_format_handler_find(HBI_MODEL_FORMAT_SAFETENSORS) == NULL,
           "find unregistered returns NULL");

    hbi_format_handler_registry_clear();
    return 0;
}

static int test_load_pipeline_with_mock(void) {
    hbi_format_handler_registry_clear();
    hbi_format_handler_register(&g_mock_handler);

    hbi_load_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.model_path = "test_model.mock";
    opts.format_hint = HBI_MODEL_FORMAT_UNKNOWN; /* auto-detect */

    hbi_load_session *session = NULL;
    hbi_status st = hbi_model_load(&opts, hbi_allocator_system(), &session);
    ASSERT(st == HBI_OK, "model load succeeds");
    ASSERT(session != NULL, "session not NULL");

    /* Check format. */
    ASSERT(hbi_load_session_format(session) == HBI_MODEL_FORMAT_GGUF,
           "session format is GGUF (mock)");

    /* Check manifest. */
    const hbi_model_manifest *m = hbi_load_session_manifest(session);
    ASSERT(m != NULL, "session manifest not NULL");
    ASSERT(hbi_model_manifest_count(m) == 2, "manifest has 2 tensors");

    const hbi_tensor_entry *emb = hbi_model_manifest_find(m, "model.embed_tokens.weight");
    ASSERT(emb != NULL, "find embed_tokens");
    ASSERT(emb->dtype == HBI_DTYPE_FP32, "embed dtype is FP32");
    ASSERT(emb->residency == HBI_RESIDENCY_RESIDENT, "embed is resident");

    const hbi_tensor_entry *qp =
        hbi_model_manifest_find(m, "model.layers.0.self_attn.q_proj.weight");
    ASSERT(qp != NULL, "find q_proj");
    ASSERT(qp->dtype == HBI_DTYPE_FP16, "q_proj dtype is FP16");
    ASSERT(qp->residency == HBI_RESIDENCY_STREAMABLE, "q_proj is streamable");

    /* Check metadata. */
    const hbi_model_metadata *md = hbi_load_session_metadata(session);
    ASSERT(md != NULL, "session metadata not NULL");
    ASSERT(hbi_model_metadata_count(md) == 2, "metadata has 2 entries");
    const char *arch = hbi_model_metadata_get(md, "architecture");
    ASSERT(arch != NULL && strcmp(arch, "llama") == 0, "architecture = llama");

    /* Check statistics. */
    const hbi_load_statistics *stats = hbi_load_session_statistics(session);
    ASSERT(stats != NULL, "statistics not NULL");
    ASSERT(stats->tensors_registered == 2, "2 tensors registered");
    ASSERT(stats->bytes_indexed > 0, "bytes indexed > 0");
    ASSERT(stats->total_load_time_ns > 0, "total load time > 0");

    hbi_model_load_session_destroy(session);
    hbi_format_handler_registry_clear();
    return 0;
}

static int test_load_pipeline_errors(void) {
    hbi_format_handler_registry_clear();

    /* NULL options. */
    hbi_load_session *session = NULL;
    hbi_status st = hbi_model_load(NULL, hbi_allocator_system(), &session);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL options rejected");

    /* Empty path. */
    hbi_load_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.model_path = "";
    st = hbi_model_load(&opts, hbi_allocator_system(), &session);
    ASSERT(st == HBI_ERR_INVALID_ARG, "empty path rejected");

    /* No handler registered, auto-detect fails. */
    opts.model_path = "model.mock";
    st = hbi_model_load(&opts, hbi_allocator_system(), &session);
    ASSERT(st == HBI_ERR_UNSUPPORTED, "no handler = unsupported");

    hbi_format_handler_registry_clear();
    return 0;
}

static int test_selftest(void) {
    ASSERT(hbi_model_selftest() == HBI_OK, "selftest passes");
    ASSERT(strcmp(hbi_model_name(), "model") == 0, "module name");
    return 0;
}

int main(void) {
    hbi_error_clear();

    int failures = 0;

    failures += test_manifest_lifecycle();
    failures += test_manifest_duplicate_rejected();
    failures += test_manifest_validation();
    failures += test_metadata_lifecycle();
    failures += test_format_handler_registry();
    failures += test_load_pipeline_with_mock();
    failures += test_load_pipeline_errors();
    failures += test_selftest();

    if (failures == 0) {
        printf("[ok] %s\n", hbi_model_name());
    } else {
        fprintf(stderr, "%d test(s) failed\n", failures);
    }
    return failures;
}

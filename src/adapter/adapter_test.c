/* adapter_test.c — comprehensive tests for the Model Adapter Framework (RFC-014). */
#include "adapter/adapter.h"
#include "adapter/adapter_internal.h"
#include "graph/graph.h"
#include "memory/memory.h"
#include "model/model.h"

#include <stdio.h>
#include <string.h>

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

/* ── Helper: set up mock metadata on a model_metadata object ───────────── */

static hbi_status setup_mock_metadata(hbi_model_metadata *md) {
    hbi_status st;
    st = hbi_model_metadata_set(md, "architecture", "mock");
    if (st != HBI_OK)
        return st;
    st = hbi_model_metadata_set(md, "hidden_size", "256");
    if (st != HBI_OK)
        return st;
    st = hbi_model_metadata_set(md, "num_attention_heads", "4");
    if (st != HBI_OK)
        return st;
    st = hbi_model_metadata_set(md, "num_layers", "2");
    if (st != HBI_OK)
        return st;
    st = hbi_model_metadata_set(md, "vocab_size", "1024");
    if (st != HBI_OK)
        return st;
    return HBI_OK;
}

/* ── Helper: set up mock manifest with required tensors ────────────────── */

static hbi_status setup_mock_manifest(hbi_model_manifest *m) {
    hbi_tensor_entry e;
    memset(&e, 0, sizeof(e));
    strncpy(e.name, "model.embed_tokens.weight", HBI_TENSOR_NAME_MAX - 1);
    e.shape.rank = 2;
    e.shape.dims[0] = 1024;
    e.shape.dims[1] = 256;
    e.dtype = HBI_DTYPE_FP32;
    e.byte_size = 1024u * 256u * 4u;
    e.required_alignment = 64;
    e.residency = HBI_RESIDENCY_RESIDENT;
    return hbi_model_manifest_add(m, &e);
}

/* ── Tests ─────────────────────────────────────────────────────────────── */

static int test_enum_strings(void) {
    /* Layer types. */
    ASSERT(strcmp(hbi_layer_type_str(HBI_LAYER_EMBEDDING), "embedding") == 0, "layer embedding");
    ASSERT(strcmp(hbi_layer_type_str(HBI_LAYER_ATTENTION), "attention") == 0, "layer attention");
    ASSERT(strcmp(hbi_layer_type_str(HBI_LAYER_MOE), "moe") == 0, "layer moe");
    ASSERT(strcmp(hbi_layer_type_str(HBI_LAYER_INVALID), "invalid") == 0, "layer invalid");
    ASSERT(strcmp(hbi_layer_type_str((hbi_layer_type)999), "invalid") == 0, "layer oor");

    /* Attention variants. */
    ASSERT(strcmp(hbi_attention_variant_str(HBI_ATTENTION_MHA), "mha") == 0, "attn mha");
    ASSERT(strcmp(hbi_attention_variant_str(HBI_ATTENTION_GQA), "gqa") == 0, "attn gqa");
    ASSERT(strcmp(hbi_attention_variant_str(HBI_ATTENTION_MLA), "mla") == 0, "attn mla");
    ASSERT(strcmp(hbi_attention_variant_str(HBI_ATTENTION_INVALID), "invalid") == 0,
           "attn invalid");

    /* Norm types. */
    ASSERT(strcmp(hbi_norm_type_str(HBI_NORM_RMSNORM), "rmsnorm") == 0, "norm rmsnorm");
    ASSERT(strcmp(hbi_norm_type_str(HBI_NORM_LAYERNORM), "layernorm") == 0, "norm layernorm");

    /* Activation types. */
    ASSERT(strcmp(hbi_adapter_activation_str(HBI_ADAPTER_ACT_GELU), "gelu") == 0, "act gelu");
    ASSERT(strcmp(hbi_adapter_activation_str(HBI_ADAPTER_ACT_SILU), "silu") == 0, "act silu");
    ASSERT(strcmp(hbi_adapter_activation_str(HBI_ADAPTER_ACT_SWIGLU), "swiglu") == 0, "act swiglu");

    /* Architectures. */
    ASSERT(strcmp(hbi_adapter_architecture_str(HBI_ADAPTER_ARCH_GENERIC), "generic") == 0,
           "arch generic");
    ASSERT(strcmp(hbi_adapter_architecture_str(HBI_ADAPTER_ARCH_TRANSFORMER_DENSE),
                  "transformer_dense") == 0,
           "arch dense");
    ASSERT(strcmp(hbi_adapter_architecture_str(HBI_ADAPTER_ARCH_TRANSFORMER_MOE),
                  "transformer_moe") == 0,
           "arch moe");

    return 0;
}

static int test_descriptor_layer_mask(void) {
    hbi_model_descriptor desc;
    memset(&desc, 0, sizeof(desc));

    ASSERT(!hbi_descriptor_has_layer(&desc, HBI_LAYER_ATTENTION), "initially no attention");

    hbi_descriptor_set_layer(&desc, HBI_LAYER_ATTENTION);
    ASSERT(hbi_descriptor_has_layer(&desc, HBI_LAYER_ATTENTION), "attention set");
    ASSERT(!hbi_descriptor_has_layer(&desc, HBI_LAYER_MOE), "moe not set");

    hbi_descriptor_set_layer(&desc, HBI_LAYER_MOE);
    ASSERT(hbi_descriptor_has_layer(&desc, HBI_LAYER_MOE), "moe set");
    ASSERT(hbi_descriptor_has_layer(&desc, HBI_LAYER_ATTENTION), "attention still set");

    /* Edge cases. */
    hbi_descriptor_set_layer(NULL, HBI_LAYER_ATTENTION); /* no crash */
    ASSERT(!hbi_descriptor_has_layer(NULL, HBI_LAYER_ATTENTION), "NULL desc returns false");
    ASSERT(!hbi_descriptor_has_layer(&desc, HBI_LAYER_INVALID), "INVALID returns false");
    ASSERT(!hbi_descriptor_has_layer(&desc, HBI_LAYER_COUNT), "COUNT returns false");

    return 0;
}

static int test_adapter_registration(void) {
    hbi_adapter_registry_clear();
    ASSERT(hbi_adapter_count() == 0, "registry starts empty");

    hbi_status st = hbi_adapter_mock_register();
    ASSERT(st == HBI_OK, "register mock adapter");
    ASSERT(hbi_adapter_count() == 1, "count == 1");

    /* Duplicate name rejected. */
    st = hbi_adapter_mock_register();
    ASSERT(st == HBI_ERR_STATE, "duplicate name rejected");
    ASSERT(hbi_adapter_count() == 1, "count stays 1");

    /* NULL rejected. */
    st = hbi_adapter_register(NULL);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL adapter rejected");

    hbi_adapter_registry_clear();
    return 0;
}

static int test_adapter_find(void) {
    hbi_adapter_registry_clear();
    hbi_adapter_mock_register();

    /* Find by name. */
    const hbi_model_adapter *a = hbi_adapter_find("mock");
    ASSERT(a != NULL, "find mock by name");
    ASSERT(strcmp(a->name, "mock") == 0, "name matches");
    ASSERT(a->architecture == HBI_ADAPTER_ARCH_GENERIC, "arch is generic");

    /* Find non-existent. */
    ASSERT(hbi_adapter_find("llama") == NULL, "find nonexistent returns NULL");
    ASSERT(hbi_adapter_find(NULL) == NULL, "find NULL returns NULL");

    /* Find by arch. */
    a = hbi_adapter_find_by_arch(HBI_ADAPTER_ARCH_GENERIC);
    ASSERT(a != NULL, "find by arch GENERIC");
    ASSERT(strcmp(a->name, "mock") == 0, "arch find returns mock");

    ASSERT(hbi_adapter_find_by_arch(HBI_ADAPTER_ARCH_TRANSFORMER_MOE) == NULL,
           "find unregistered arch returns NULL");

    hbi_adapter_registry_clear();
    return 0;
}

static int test_adapter_validate_metadata(void) {
    hbi_adapter_registry_clear();
    hbi_adapter_mock_register();

    const hbi_model_adapter *mock = hbi_adapter_mock_get();
    ASSERT(mock != NULL, "mock getter works");

    /* Valid metadata. */
    hbi_model_metadata *md = NULL;
    hbi_model_metadata_create(hbi_allocator_system(), &md);
    setup_mock_metadata(md);

    hbi_status st = mock->validate_metadata(mock, md);
    ASSERT(st == HBI_OK, "valid metadata passes");

    /* Missing architecture. */
    hbi_model_metadata *md2 = NULL;
    hbi_model_metadata_create(hbi_allocator_system(), &md2);
    hbi_model_metadata_set(md2, "hidden_size", "256");
    st = mock->validate_metadata(mock, md2);
    ASSERT(st == HBI_ERR_NOT_FOUND, "missing architecture rejected");

    /* Wrong architecture. */
    hbi_model_metadata *md3 = NULL;
    hbi_model_metadata_create(hbi_allocator_system(), &md3);
    hbi_model_metadata_set(md3, "architecture", "llama");
    hbi_model_metadata_set(md3, "hidden_size", "256");
    hbi_model_metadata_set(md3, "num_attention_heads", "4");
    hbi_model_metadata_set(md3, "num_layers", "2");
    hbi_model_metadata_set(md3, "vocab_size", "1024");
    st = mock->validate_metadata(mock, md3);
    ASSERT(st == HBI_ERR_NOT_FOUND, "wrong architecture rejected");

    /* Missing required key. */
    hbi_model_metadata *md4 = NULL;
    hbi_model_metadata_create(hbi_allocator_system(), &md4);
    hbi_model_metadata_set(md4, "architecture", "mock");
    hbi_model_metadata_set(md4, "hidden_size", "256");
    /* Missing num_attention_heads, num_layers, vocab_size. */
    st = mock->validate_metadata(mock, md4);
    ASSERT(st == HBI_ERR_NOT_FOUND, "missing required key rejected");

    /* Invalid numeric value. */
    hbi_model_metadata *md5 = NULL;
    hbi_model_metadata_create(hbi_allocator_system(), &md5);
    hbi_model_metadata_set(md5, "architecture", "mock");
    hbi_model_metadata_set(md5, "hidden_size", "not_a_number");
    hbi_model_metadata_set(md5, "num_attention_heads", "4");
    hbi_model_metadata_set(md5, "num_layers", "2");
    hbi_model_metadata_set(md5, "vocab_size", "1024");
    st = mock->validate_metadata(mock, md5);
    ASSERT(st == HBI_ERR_CORRUPT, "invalid numeric value rejected");

    /* NULL metadata. */
    st = mock->validate_metadata(mock, NULL);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL metadata rejected");

    hbi_model_metadata_destroy(md);
    hbi_model_metadata_destroy(md2);
    hbi_model_metadata_destroy(md3);
    hbi_model_metadata_destroy(md4);
    hbi_model_metadata_destroy(md5);
    hbi_adapter_registry_clear();
    return 0;
}

static int test_adapter_build_descriptor(void) {
    const hbi_model_adapter *mock = hbi_adapter_mock_get();

    hbi_model_metadata *md = NULL;
    hbi_model_metadata_create(hbi_allocator_system(), &md);
    setup_mock_metadata(md);

    hbi_model_descriptor desc;
    hbi_status st = mock->build_descriptor(mock, md, &desc);
    ASSERT(st == HBI_OK, "build descriptor");
    ASSERT(strcmp(desc.architecture_name, "mock") == 0, "arch name");
    ASSERT(desc.architecture == HBI_ADAPTER_ARCH_TRANSFORMER_DENSE, "arch enum");
    ASSERT(desc.hidden_size == 256, "hidden_size");
    ASSERT(desc.num_attention_heads == 4, "num_heads");
    ASSERT(desc.num_layers == 2, "num_layers");
    ASSERT(desc.vocab_size == 1024, "vocab_size");
    ASSERT(desc.num_kv_heads == 4, "kv_heads (MHA = same as heads)");
    ASSERT(desc.intermediate_size == 256 * 4, "intermediate_size = 4x hidden");
    ASSERT(desc.attention_variant == HBI_ATTENTION_MHA, "attention MHA");
    ASSERT(desc.norm_type == HBI_NORM_RMSNORM, "norm rmsnorm");
    ASSERT(desc.activation == HBI_ADAPTER_ACT_SILU, "activation silu");

    /* Layer types set. */
    ASSERT(hbi_descriptor_has_layer(&desc, HBI_LAYER_EMBEDDING), "has embedding");
    ASSERT(hbi_descriptor_has_layer(&desc, HBI_LAYER_ATTENTION), "has attention");
    ASSERT(hbi_descriptor_has_layer(&desc, HBI_LAYER_FEED_FORWARD), "has FFN");
    ASSERT(hbi_descriptor_has_layer(&desc, HBI_LAYER_NORMALIZATION), "has norm");
    ASSERT(hbi_descriptor_has_layer(&desc, HBI_LAYER_OUTPUT_HEAD), "has output head");
    ASSERT(hbi_descriptor_has_layer(&desc, HBI_LAYER_RESIDUAL), "has residual");
    ASSERT(!hbi_descriptor_has_layer(&desc, HBI_LAYER_MOE), "no MoE (dense model)");

    /* NULL args. */
    st = mock->build_descriptor(mock, NULL, &desc);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL metadata rejected");
    st = mock->build_descriptor(mock, md, NULL);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL descriptor rejected");

    hbi_model_metadata_destroy(md);
    return 0;
}

static int test_adapter_register_tensors(void) {
    const hbi_model_adapter *mock = hbi_adapter_mock_get();

    /* Valid manifest with embedding tensor. */
    hbi_model_manifest *m = NULL;
    hbi_model_manifest_create(hbi_allocator_system(), &m);
    setup_mock_manifest(m);

    hbi_model_descriptor desc;
    memset(&desc, 0, sizeof(desc));
    desc.hidden_size = 256;
    desc.vocab_size = 1024;

    hbi_status st = mock->register_tensors(mock, m, &desc);
    ASSERT(st == HBI_OK, "register tensors succeeds");

    /* Missing embedding tensor. */
    hbi_model_manifest *m2 = NULL;
    hbi_model_manifest_create(hbi_allocator_system(), &m2);
    hbi_tensor_entry e;
    memset(&e, 0, sizeof(e));
    strncpy(e.name, "model.layers.0.weight", HBI_TENSOR_NAME_MAX - 1);
    e.shape.rank = 2;
    e.shape.dims[0] = 256;
    e.shape.dims[1] = 256;
    e.dtype = HBI_DTYPE_FP32;
    e.byte_size = 256 * 256 * 4;
    hbi_model_manifest_add(m2, &e);

    st = mock->register_tensors(mock, m2, &desc);
    ASSERT(st == HBI_ERR_NOT_FOUND, "missing embedding tensor rejected");

    /* NULL args. */
    st = mock->register_tensors(mock, NULL, &desc);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL manifest rejected");
    st = mock->register_tensors(mock, m, NULL);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL descriptor rejected");

    hbi_model_manifest_destroy(m);
    hbi_model_manifest_destroy(m2);
    return 0;
}

static int test_adapter_graph_construction(void) {
    const hbi_model_adapter *mock = hbi_adapter_mock_get();

    hbi_model_descriptor desc;
    memset(&desc, 0, sizeof(desc));
    desc.hidden_size = 128;
    desc.num_layers = 2;

    hbi_graph_builder *builder = NULL;
    hbi_status st = hbi_graph_builder_create(&builder);
    ASSERT(st == HBI_OK, "graph builder create");

    st = mock->build_graph(mock, builder, &desc);
    ASSERT(st == HBI_OK, "mock build_graph succeeds");

    /* Finalize the graph. */
    hbi_graph *graph = NULL;
    st = hbi_graph_build(builder, &graph);
    ASSERT(st == HBI_OK, "graph build (finalize)");
    ASSERT(hbi_graph_num_nodes(graph) == 2, "2 nodes in graph");
    ASSERT(hbi_graph_num_values(graph) == 3, "3 values in graph");

    /* Check execution order exists. */
    const uint32_t *order = hbi_graph_execution_order(graph);
    ASSERT(order != NULL, "execution order exists");

    hbi_graph_destroy(graph);

    /* NULL args. */
    st = mock->build_graph(mock, NULL, &desc);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL builder rejected");
    st = mock->build_graph(mock, builder, NULL); /* builder was consumed, create a new one */

    hbi_adapter_registry_clear();
    return 0;
}

static int test_adapter_capabilities(void) {
    const hbi_model_adapter *mock = hbi_adapter_mock_get();

    uint32_t caps = mock->get_capabilities(mock);
    ASSERT(caps & HBI_CAP_SUPPORTS_BATCHED_INFERENCE, "supports batched inference");
    ASSERT(caps & HBI_CAP_SUPPORTS_QUANTIZED_WEIGHTS, "supports quantized weights");
    ASSERT(!(caps & HBI_CAP_SUPPORTS_SPARSE_MOE), "no sparse MoE (dense model)");
    ASSERT(!(caps & HBI_CAP_SUPPORTS_KV_COMPRESSION), "no KV compression");

    return 0;
}

static int test_adapter_context_lifecycle(void) {
    const hbi_model_adapter *mock = hbi_adapter_mock_get();
    hbi_allocator *alloc = hbi_allocator_system();

    hbi_model_descriptor desc;
    memset(&desc, 0, sizeof(desc));
    strncpy(desc.architecture_name, "mock", HBI_ADAPTER_NAME_MAX - 1);
    desc.hidden_size = 256;
    desc.num_layers = 2;
    desc.num_attention_heads = 4;
    desc.vocab_size = 1024;

    hbi_model_context *ctx = NULL;
    hbi_status st = mock->create_context(mock, &desc, alloc, &ctx);
    ASSERT(st == HBI_OK, "create context");
    ASSERT(ctx != NULL, "context not NULL");
    ASSERT(ctx->initialized, "context initialized flag");
    ASSERT(ctx->adapter == mock, "context back-pointer");
    ASSERT(ctx->descriptor.hidden_size == 256, "descriptor copied");

    /* Statistics. */
    hbi_model_statistics stats;
    st = mock->get_statistics(mock, &stats);
    ASSERT(st == HBI_OK, "get statistics");
    ASSERT(stats.adapter_memory_bytes > 0, "memory accounted");

    /* Destroy context. */
    mock->destroy_context(mock, ctx);
    /* NULL-safe destroy. */
    mock->destroy_context(mock, NULL);

    /* NULL args. */
    st = mock->create_context(mock, NULL, alloc, &ctx);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL descriptor rejected");
    st = mock->create_context(mock, &desc, NULL, &ctx);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL allocator rejected");
    st = mock->create_context(mock, &desc, alloc, NULL);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL out rejected");

    /* NULL statistics. */
    st = mock->get_statistics(mock, NULL);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL stats rejected");

    return 0;
}

static int test_adapter_shutdown(void) {
    const hbi_model_adapter *mock = hbi_adapter_mock_get();

    /* Shutdown should not crash, even when called multiple times. */
    mock->shutdown(mock);
    mock->shutdown(mock);
    mock->shutdown(NULL); /* NULL-safe. */

    return 0;
}

static int test_adapter_resolve(void) {
    hbi_adapter_registry_clear();
    hbi_adapter_mock_register();

    /* Create a mock load session with matching metadata.
     * We build the session manually using the model loader's public API. */
    hbi_model_metadata *md = NULL;
    hbi_model_metadata_create(hbi_allocator_system(), &md);
    hbi_model_metadata_set(md, "architecture", "mock");

    /* We can't easily create a full hbi_load_session from outside the model
     * module, so we test hbi_adapter_resolve with NULL. */
    const hbi_model_adapter *adapter = NULL;
    hbi_status st = hbi_adapter_resolve(NULL, &adapter);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL session rejected");

    st = hbi_adapter_resolve((const hbi_load_session *)0x1, NULL);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL out_adapter rejected");

    hbi_model_metadata_destroy(md);
    hbi_adapter_registry_clear();
    return 0;
}

static int test_adapter_init_model_errors(void) {
    hbi_adapter_registry_clear();
    hbi_adapter_mock_register();

    const hbi_model_adapter *mock = hbi_adapter_mock_get();
    hbi_model_descriptor desc;
    hbi_model_statistics stats;

    /* NULL args. */
    hbi_status st = hbi_adapter_init_model(NULL, NULL, hbi_allocator_system(), &desc, &stats);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL adapter rejected");

    st = hbi_adapter_init_model(mock, NULL, hbi_allocator_system(), &desc, &stats);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL session rejected");

    st = hbi_adapter_init_model(mock, (const hbi_load_session *)0x1, hbi_allocator_system(), NULL,
                                &stats);
    ASSERT(st == HBI_ERR_INVALID_ARG, "NULL descriptor rejected");

    hbi_adapter_registry_clear();
    return 0;
}

static int test_vtable_validation(void) {
    /* Adapter with missing vtable fields should be rejected. */
    hbi_adapter_registry_clear();

    hbi_model_adapter incomplete;
    memset(&incomplete, 0, sizeof(incomplete));
    incomplete.name = "incomplete";
    /* Missing all vtable fields. */

    hbi_status st = hbi_adapter_register(&incomplete);
    ASSERT(st == HBI_ERR_INVALID_ARG, "incomplete adapter rejected");

    /* Adapter with no name. */
    hbi_model_adapter no_name;
    memset(&no_name, 0, sizeof(no_name));
    no_name.init =
        (hbi_status(*)(const hbi_model_adapter *, const hbi_load_session *, hbi_allocator *))0x1;
    /* All other fields NULL. */
    st = hbi_adapter_register(&no_name);
    ASSERT(st == HBI_ERR_INVALID_ARG, "nameless adapter rejected");

    hbi_adapter_registry_clear();
    return 0;
}

static int test_selftest(void) {
    ASSERT(hbi_adapter_selftest() == HBI_OK, "selftest passes");
    ASSERT(strcmp(hbi_adapter_name(), "adapter") == 0, "module name");
    return 0;
}

int main(void) {
    hbi_error_clear();

    int failures = 0;

    failures += test_enum_strings();
    failures += test_descriptor_layer_mask();
    failures += test_adapter_registration();
    failures += test_adapter_find();
    failures += test_adapter_validate_metadata();
    failures += test_adapter_build_descriptor();
    failures += test_adapter_register_tensors();
    failures += test_adapter_graph_construction();
    failures += test_adapter_capabilities();
    failures += test_adapter_context_lifecycle();
    failures += test_adapter_shutdown();
    failures += test_adapter_resolve();
    failures += test_adapter_init_model_errors();
    failures += test_vtable_validation();
    failures += test_selftest();

    if (failures == 0) {
        printf("[ok] %s\n", hbi_adapter_name());
    } else {
        fprintf(stderr, "%d test(s) failed\n", failures);
    }
    return failures;
}

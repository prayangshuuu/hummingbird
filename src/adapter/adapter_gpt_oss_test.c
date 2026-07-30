/* adapter_gpt_oss_test.c — Isolated test for GPT-OSS adapter forward pass */

#include "adapter/adapter_gpt_oss.h"
#include "adapter/adapter_internal.h"
#include "cpu/backend_cpu.h"
#include "kernel/kernel.h"
#include "memory/memory.h"
#include "model/model.h"
#include "platform/platform.h"
#include "tensor/tensor.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

/* Redefine internal types from adapter_gpt_oss.c to inspect internals */
typedef struct gpt_oss_layer_weights {
    hbi_tensor q_proj;
    hbi_tensor k_proj;
    hbi_tensor v_proj;
    hbi_tensor o_proj;
    hbi_tensor input_norm;
    hbi_tensor post_norm;
    hbi_tensor gate_proj;
    hbi_tensor up_proj;
    hbi_tensor down_proj;
} gpt_oss_layer_weights;

typedef struct gpt_oss_context {
    hbi_allocator *allocator;
    const hbi_model_descriptor *descriptor;
    hbi_tensor embed_tokens;
    hbi_tensor final_norm;
    hbi_tensor lm_head;
    gpt_oss_layer_weights *layers;
    uint32_t num_layers;
    uint32_t hidden_size;
    uint32_t num_q_heads;
    uint32_t num_kv_heads;
    uint32_t head_dim;
    uint32_t intermediate_size;
    uint32_t vocab_size;
    float rms_norm_eps;
    float rope_theta;
    float *scratch_a;
    float *scratch_b;
    float *scratch_c;
    float *scratch_d;
    float *attn_q;
    float *attn_k;
    float *attn_v;
    float *attn_out;
    float *logits;
    void *kv_manager;
    void *kv_handle;
    bool initialized;
} gpt_oss_context;

/* Forward declare adapter APIs */
hbi_status hbi_adapter_gpt_oss_bind_weights(hbi_model_context *fctx, const char *name,
                                            const void *data, const hbi_shape *shape);
hbi_status hbi_adapter_gpt_oss_forward(struct gpt_oss_context *ctx, uint32_t token_id, uint32_t pos,
                                       uint32_t *out_token);

static float *allocate_weight(hbi_allocator *alloc, size_t count) {
    float *ptr = hbi_alloc(alloc, count * sizeof(float), 64, HBI_MEM_GENERAL);
    for (size_t i = 0; i < count; i++) {
        ptr[i] = 0.01f; /* Small non-zero value */
    }
    return ptr;
}

static int test_gpt_oss_forward(void) {
    hbi_adapter_registry_clear();
    ASSERT(hbi_adapter_gpt_oss_register() == HBI_OK, "register GPT-OSS adapter");
    const hbi_model_adapter *adapter = hbi_adapter_gpt_oss_get();
    ASSERT(adapter != NULL, "get adapter");

    hbi_model_metadata *md = NULL;
    hbi_model_metadata_create(hbi_allocator_system(), &md);
    hbi_model_metadata_set(md, "architecture", "gpt_oss");
    hbi_model_metadata_set(md, "hidden_size", "64");
    hbi_model_metadata_set(md, "intermediate_size", "256");
    hbi_model_metadata_set(md, "num_attention_heads", "4");
    hbi_model_metadata_set(md, "num_key_value_heads", "4");
    hbi_model_metadata_set(md, "num_hidden_layers", "2");
    hbi_model_metadata_set(md, "vocab_size", "256");
    hbi_model_metadata_set(md, "max_position_embeddings", "64");
    hbi_model_metadata_set(md, "rms_norm_eps", "1e-5");

    hbi_model_descriptor desc;
    ASSERT(adapter->build_descriptor(adapter, md, &desc) == HBI_OK, "build descriptor");
    hbi_model_metadata_destroy(md);

    hbi_allocator *alloc = hbi_allocator_system();
    hbi_model_context *ctx = NULL;
    ASSERT(adapter->create_context(adapter, &desc, alloc, &ctx) == HBI_OK, "create context");

    uint32_t H = 64, IS = 256, V = 256, L = 2;
    hbi_shape s_embed = {.rank = 2, .dims = {V, H}};
    hbi_shape s_norm = {.rank = 1, .dims = {H}};
    hbi_shape s_q = {.rank = 2, .dims = {H, H}};
    hbi_shape s_o = {.rank = 2, .dims = {H, H}};
    hbi_shape s_gate = {.rank = 2, .dims = {IS, H}};
    hbi_shape s_down = {.rank = 2, .dims = {H, IS}};

    float *w_embed = allocate_weight(alloc, V * H);
    float *w_norm = allocate_weight(alloc, H);
    float *w_head = allocate_weight(alloc, V * H);

    ASSERT(hbi_adapter_gpt_oss_bind_weights(ctx, "model.embed_tokens.weight", w_embed, &s_embed) ==
               HBI_OK,
           "bind embed");
    ASSERT(hbi_adapter_gpt_oss_bind_weights(ctx, "model.norm.weight", w_norm, &s_norm) == HBI_OK,
           "bind fnorm");
    ASSERT(hbi_adapter_gpt_oss_bind_weights(ctx, "lm_head.weight", w_head, &s_embed) == HBI_OK,
           "bind lm_head");

    float **w_layers = hbi_alloc(alloc, L * 9 * sizeof(float *), 8, HBI_MEM_GENERAL);
    for (uint32_t i = 0; i < L; i++) {
        char name[128];

        w_layers[i * 9 + 0] = allocate_weight(alloc, H);
        snprintf(name, sizeof(name), "model.layers.%u.input_layernorm.weight", i);
        ASSERT(hbi_adapter_gpt_oss_bind_weights(ctx, name, w_layers[i * 9 + 0], &s_norm) == HBI_OK,
               "bind inorm");

        w_layers[i * 9 + 1] = allocate_weight(alloc, H);
        snprintf(name, sizeof(name), "model.layers.%u.post_attention_layernorm.weight", i);
        ASSERT(hbi_adapter_gpt_oss_bind_weights(ctx, name, w_layers[i * 9 + 1], &s_norm) == HBI_OK,
               "bind pnorm");

        w_layers[i * 9 + 2] = allocate_weight(alloc, H * H);
        snprintf(name, sizeof(name), "model.layers.%u.self_attn.q_proj.weight", i);
        ASSERT(hbi_adapter_gpt_oss_bind_weights(ctx, name, w_layers[i * 9 + 2], &s_q) == HBI_OK,
               "bind q");

        w_layers[i * 9 + 3] = allocate_weight(alloc, H * H);
        snprintf(name, sizeof(name), "model.layers.%u.self_attn.k_proj.weight", i);
        ASSERT(hbi_adapter_gpt_oss_bind_weights(ctx, name, w_layers[i * 9 + 3], &s_q) == HBI_OK,
               "bind k");

        w_layers[i * 9 + 4] = allocate_weight(alloc, H * H);
        snprintf(name, sizeof(name), "model.layers.%u.self_attn.v_proj.weight", i);
        ASSERT(hbi_adapter_gpt_oss_bind_weights(ctx, name, w_layers[i * 9 + 4], &s_q) == HBI_OK,
               "bind v");

        w_layers[i * 9 + 5] = allocate_weight(alloc, H * H);
        snprintf(name, sizeof(name), "model.layers.%u.self_attn.o_proj.weight", i);
        ASSERT(hbi_adapter_gpt_oss_bind_weights(ctx, name, w_layers[i * 9 + 5], &s_o) == HBI_OK,
               "bind o");

        w_layers[i * 9 + 6] = allocate_weight(alloc, IS * H);
        snprintf(name, sizeof(name), "model.layers.%u.mlp.gate_proj.weight", i);
        ASSERT(hbi_adapter_gpt_oss_bind_weights(ctx, name, w_layers[i * 9 + 6], &s_gate) == HBI_OK,
               "bind gate");

        w_layers[i * 9 + 7] = allocate_weight(alloc, IS * H);
        snprintf(name, sizeof(name), "model.layers.%u.mlp.up_proj.weight", i);
        ASSERT(hbi_adapter_gpt_oss_bind_weights(ctx, name, w_layers[i * 9 + 7], &s_gate) == HBI_OK,
               "bind up");

        w_layers[i * 9 + 8] = allocate_weight(alloc, H * IS);
        snprintf(name, sizeof(name), "model.layers.%u.mlp.down_proj.weight", i);
        ASSERT(hbi_adapter_gpt_oss_bind_weights(ctx, name, w_layers[i * 9 + 8], &s_down) == HBI_OK,
               "bind down");
    }

    /* We also need to manually patch ctx->rms_norm_eps and ctx->rope_theta since we skipped init()
     */
    gpt_oss_context *gctx = (gpt_oss_context *)ctx->private_data;
    gctx->rms_norm_eps = 1e-5f;
    gctx->rope_theta = 10000.0f;

    /* Forward pass on dummy tokens */
    uint32_t tokens[] = {0, 1, 2};
    for (uint32_t pos = 0; pos < 3; pos++) {
        uint32_t out_token = 0;
        hbi_status st = hbi_adapter_gpt_oss_forward(gctx, tokens[pos], pos, &out_token);
        ASSERT(st == HBI_OK, "forward pass");

        /* Verify logits are finite */
        for (uint32_t v = 0; v < V; v++) {
            float logit = gctx->logits[v];
            ASSERT(!isnan(logit), "logit is NaN");
            ASSERT(!isinf(logit), "logit is Inf");
        }
    }

    /* Cleanup */
    adapter->destroy_context(adapter, ctx);
    hbi_free(alloc, w_embed);
    hbi_free(alloc, w_norm);
    hbi_free(alloc, w_head);
    for (uint32_t i = 0; i < L; i++) {
        for (uint32_t j = 0; j < 9; j++) {
            hbi_free(alloc, w_layers[i * 9 + j]);
        }
    }
    hbi_free(alloc, w_layers);

    return 0;
}

int main(void) {
    hbi_error_clear();
    int failures = 0;

    if (hb_backend_cpu_register() != HBI_OK)
        failures++;
    failures += test_gpt_oss_forward();

    if (failures == 0) {
        printf("[ok] adapter_gpt_oss_test\n");
    } else {
        fprintf(stderr, "%d test(s) failed\n", failures);
    }
    return failures;
}

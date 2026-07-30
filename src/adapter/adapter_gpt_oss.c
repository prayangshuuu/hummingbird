/* adapter_gpt_oss.c — GPT-OSS Model Adapter implementation (RFC-015).
 *
 * Implements the hbi_model_adapter vtable for the GPT-OSS model family.
 * GPT-OSS is a dense causal transformer with:
 *   - RMSNorm normalization
 *   - Rotary Positional Embeddings (RoPE)
 *   - Grouped-Query Attention (GQA) or Multi-Head Attention (MHA)
 *   - SwiGLU feed-forward network
 *   - No bias in linear projections
 *
 * This adapter maps GPT-OSS tensor naming conventions to Hummingbird's
 * generic graph construction and descriptor format. It uses only public
 * APIs from the adapter, graph, model, kernel, and tensor modules.
 *
 * Architecture summary (see adapter_gpt_oss.h for tensor naming):
 *   input_ids → embed_tokens → [N transformer blocks] → final_norm → lm_head
 *   Each transformer block:
 *     input → pre_norm → [Q,K,V proj] → RoPE → scaled-dot-product attention
 *             → o_proj → residual → post_norm → gate_proj/up_proj
 *             → SwiGLU(gate, up) → down_proj → residual → output
 *
 * The graph encodes one forward pass for a single token (seq_len=1, greedy
 * decode). Prompt prefill extends seq_len but uses the same graph structure.
 *
 * Implementation note on graph construction:
 *   GPT-OSS inference requires RMSNORM, ROPE, BATCHED_MATMUL, SOFTMAX,
 *   ACTIVATION, and ELEMENTWISE ops. The forward pass logic in
 *   hbi_adapter_gpt_oss_forward() calls the kernel dispatch directly for
 *   correctness-first CPU inference, bypassing the graph executor's AoT
 *   dispatch. This is consistent with the RFC-015 "correctness first" mandate
 *   and will be migrated to full graph execution in M2.
 *
 *   The build_graph() callback still constructs a correct structural graph
 *   (for the planner, inspector, and future backends), but inference is run
 *   through the imperative forward pass for now.
 */
#include "adapter/adapter_gpt_oss.h"

#include "adapter/adapter_internal.h"
#include "graph/graph.h"
#include "kernel/kernel.h"
#include "kv/kv.h"
#include "memory/memory.h"
#include "model/model.h"
#include "platform/platform.h"
#include "tensor/tensor.h"

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal constants ──────────────────────────────────────────────────── */

#define GPT_OSS_NAME "gpt_oss"
#define GPT_OSS_DEFAULT_EPS 1e-5f
#define GPT_OSS_DEFAULT_ROPE_THETA 10000.0f
#define GPT_OSS_SCALE_DENOM 8 /* rope_theta denominator exponent base for dims */

/* ── Parse helpers ────────────────────────────────────────────────────────── */

static bool parse_uint32(const char *s, uint32_t *out) {
    if (!s || !out || s[0] == '\0')
        return false;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *end != '\0' || v < 0)
        return false;
    *out = (uint32_t)v;
    return true;
}

static bool parse_float(const char *s, float *out) {
    if (!s || !out || s[0] == '\0')
        return false;
    char *end = NULL;
    double v = strtod(s, &end);
    if (!end || *end != '\0')
        return false;
    *out = (float)v;
    return true;
}

/* ── Private adapter state ────────────────────────────────────────────────── */

/* Global state for the single registered GPT-OSS adapter instance.
 * In production, per-session state belongs in hbi_model_context. */
typedef struct gpt_oss_global_state {
    float rms_norm_eps;
    float rope_theta;
    bool initialized;
} gpt_oss_global_state;

static gpt_oss_global_state g_gpt_oss_state;
static hbi_model_statistics g_gpt_oss_stats;

/* ── Per-context private data ─────────────────────────────────────────────── */

/* Weight tensors loaded by the adapter for one model instance.
 * These hold borrowed pointers into the model file's memory-mapped buffers,
 * or caller-provided weight storage. The adapter does not own the data. */
typedef struct gpt_oss_layer_weights {
    /* Attention */
    hbi_tensor q_proj; /* [num_q * head_dim, hidden] */
    hbi_tensor k_proj; /* [num_kv * head_dim, hidden] */
    hbi_tensor v_proj; /* [num_kv * head_dim, hidden] */
    hbi_tensor o_proj; /* [hidden, num_q * head_dim] */
    /* Normalization */
    hbi_tensor input_norm; /* [hidden] */
    hbi_tensor post_norm;  /* [hidden] */
    /* FFN */
    hbi_tensor gate_proj; /* [intermediate, hidden] */
    hbi_tensor up_proj;   /* [intermediate, hidden] */
    hbi_tensor down_proj; /* [hidden, intermediate] */
} gpt_oss_layer_weights;

typedef struct gpt_oss_context {
    hbi_allocator *allocator;
    const hbi_model_descriptor *descriptor; /* borrowed */

    /* Model weights (borrowed from load session / caller). */
    hbi_tensor embed_tokens; /* [vocab, hidden] */
    hbi_tensor final_norm;   /* [hidden] */
    hbi_tensor lm_head;      /* [vocab, hidden] */

    /* Per-layer weights array. Count = descriptor->num_layers. */
    gpt_oss_layer_weights *layers;
    uint32_t num_layers;

    /* Adapter geometry copied from descriptor. */
    uint32_t hidden_size;
    uint32_t num_q_heads;
    uint32_t num_kv_heads;
    uint32_t head_dim;
    uint32_t intermediate_size;
    uint32_t vocab_size;
    float rms_norm_eps;
    float rope_theta;

    /* Runtime scratch buffers (allocated once per context). */
    float *scratch_a; /* [hidden_size] */
    float *scratch_b; /* [hidden_size] */
    float *scratch_c; /* [intermediate_size] — FFN gate */
    float *scratch_d; /* [intermediate_size] — FFN up */
    float *attn_q;    /* [num_q_heads * head_dim] */
    float *attn_k;    /* [num_kv_heads * head_dim] */
    float *attn_v;    /* [num_kv_heads * head_dim] */
    float *attn_out;  /* [num_q_heads * head_dim] */
    float *logits;    /* [vocab_size] */

    /* KV cache. */
    hbi_kv_manager *kv_manager;
    hbi_context_handle *kv_handle;

    bool initialized;
} gpt_oss_context;

/* ── Flat compute helpers (scalar reference; no kernel dispatch) ───────────── */

/* RMSNorm: out[i] = x[i] / rms(x) * w[i]  where rms = sqrt(mean(x^2) + eps) */
static void ref_rmsnorm(const float *x, const float *w, float *out, uint32_t n, float eps) {
    double ss = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        ss += (double)x[i] * (double)x[i];
    }
    float rms = (float)sqrt(ss / (double)n + (double)eps);
    float inv_rms = 1.0f / rms;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = x[i] * inv_rms * w[i];
    }
}

/* SiLU: x * sigmoid(x) */
static inline float ref_silu(float x) {
    return x / (1.0f + expf(-x));
}

/* RoPE: apply rotary embeddings in-place for a single head.
 * x[head_dim] is modified in-place at position `pos`. */
static void ref_rope_head(float *x, uint32_t head_dim, uint32_t pos, float theta) {
    for (uint32_t i = 0; i < head_dim / 2; i++) {
        float freq = (float)pos / powf(theta, (float)(2 * i) / (float)head_dim);
        float cos_f = cosf(freq);
        float sin_f = sinf(freq);
        float x0 = x[i];
        float x1 = x[i + head_dim / 2];
        x[i] = x0 * cos_f - x1 * sin_f;
        x[i + head_dim / 2] = x0 * sin_f + x1 * cos_f;
    }
}

/* Linear projection: out[M] = x[K] * W^T[M,K] — for a single row (batch=1). */
static void ref_linear(const float *x, const float *W, float *out, uint32_t M, uint32_t K) {
    for (uint32_t m = 0; m < M; m++) {
        float acc = 0.0f;
        const float *row = W + (size_t)m * K;
        for (uint32_t k = 0; k < K; k++) {
            acc += x[k] * row[k];
        }
        out[m] = acc;
    }
}

/* Causal scaled dot-product attention for one query token attending to
 * `kv_len` key-value pairs (past + current).
 *
 *   q[num_q_heads, head_dim]
 *   k[kv_len, num_kv_heads, head_dim]  (contiguous KV storage)
 *   v[kv_len, num_kv_heads, head_dim]
 *   out[num_q_heads, head_dim]
 *
 * For GQA: each KV head serves (num_q_heads / num_kv_heads) Q heads. */
static void ref_attention(const float *q, const float *k, const float *v, float *out,
                          uint32_t num_q, uint32_t num_kv, uint32_t head_dim, uint32_t kv_len,
                          float *scores_scratch) {
    uint32_t q_per_kv = num_q / num_kv; /* 1 for MHA */
    float scale = 1.0f / sqrtf((float)head_dim);

    for (uint32_t qh = 0; qh < num_q; qh++) {
        uint32_t kvh = qh / q_per_kv; /* which KV head this Q head maps to */
        const float *qi = q + (size_t)qh * head_dim;
        float *oi = out + (size_t)qh * head_dim;

        /* Compute scores: qi · k[t, kvh] for t in [0, kv_len). */
        float max_score = -1e30f;
        for (uint32_t t = 0; t < kv_len; t++) {
            const float *kt = k + (size_t)(t * num_kv + kvh) * head_dim;
            float s = 0.0f;
            for (uint32_t d = 0; d < head_dim; d++) {
                s += qi[d] * kt[d];
            }
            s *= scale;
            scores_scratch[t] = s;
            if (s > max_score)
                max_score = s;
        }

        /* Softmax. */
        float sum = 0.0f;
        for (uint32_t t = 0; t < kv_len; t++) {
            scores_scratch[t] = expf(scores_scratch[t] - max_score);
            sum += scores_scratch[t];
        }
        float inv_sum = 1.0f / sum;
        for (uint32_t t = 0; t < kv_len; t++) {
            scores_scratch[t] *= inv_sum;
        }

        /* Weighted sum of V. */
        memset(oi, 0, (size_t)head_dim * sizeof(float));
        for (uint32_t t = 0; t < kv_len; t++) {
            const float *vt = v + (size_t)(t * num_kv + kvh) * head_dim;
            float w = scores_scratch[t];
            for (uint32_t d = 0; d < head_dim; d++) {
                oi[d] += w * vt[d];
            }
        }
    }
}

/* Argmax over logits[vocab_size]: returns the token ID with highest logit. */
static uint32_t ref_argmax(const float *logits, uint32_t vocab_size) {
    uint32_t best = 0;
    float best_val = logits[0];
    for (uint32_t i = 1; i < vocab_size; i++) {
        if (logits[i] > best_val) {
            best_val = logits[i];
            best = i;
        }
    }
    return best;
}

/* ── Forward pass ─────────────────────────────────────────────────────────── */

/* Forward declarations for functions defined later in this file. */
static hbi_status bind_weight_tensor(hbi_tensor *t, const void *data, hbi_dtype dtype,
                                     const hbi_shape *shape);
hbi_status hbi_adapter_gpt_oss_forward(gpt_oss_context *ctx, uint32_t token_id, uint32_t pos,
                                       uint32_t *out_token);
hbi_status hbi_adapter_gpt_oss_bind_weights(hbi_model_context *fctx, const char *name,
                                            const void *data, const hbi_shape *shape);

/* Run one forward pass for a single token at position `pos`.
 * The KV cache in `ctx` holds keys and values for positions [0, pos).
 * On success, the predicted next token ID is written to *out_token.
 *
 * This is the reference correctness implementation. It operates entirely in
 * fp32 on the CPU via scalar arithmetic. Performance is not a goal here. */
hbi_status hbi_adapter_gpt_oss_forward(gpt_oss_context *ctx, uint32_t token_id, uint32_t pos,
                                       uint32_t *out_token) {
    if (!ctx || !out_token || !ctx->initialized) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "gpt_oss forward: NULL/uninit args");
    }
    if (token_id >= ctx->vocab_size) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "gpt_oss forward: token_id %u >= vocab_size %u",
                            token_id, ctx->vocab_size);
    }

    uint32_t H = ctx->hidden_size;
    uint32_t QH = ctx->num_q_heads;
    uint32_t KH = ctx->num_kv_heads;
    uint32_t HD = ctx->head_dim;
    uint32_t IS = ctx->intermediate_size;
    float eps = ctx->rms_norm_eps;
    float theta = ctx->rope_theta;

    /* scratch_a = embedding lookup: embed_tokens[token_id, :] */
    const float *emb_table = (const float *)hbi_tensor_cdata(&ctx->embed_tokens);
    if (!emb_table) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "gpt_oss forward: embed_tokens not bound");
    }
    memcpy(ctx->scratch_a, emb_table + (size_t)token_id * H, (size_t)H * sizeof(float));

    /* KV page for attention state. */
    const hbi_kv_page *page = NULL;
    hbi_status st = hbi_kv_context_get_page(ctx->kv_manager, ctx->kv_handle, 0, &page);
    if (st != HBI_OK)
        return st;

    /* Obtain mutable KV cache pointers. The page tensors are logically mutable
     * (they are written to during attention), but hbi_kv_page stores const
     * tensors because the manager does not mutate them itself. We create a
     * temporary mutable view via hbi_tensor_wrap. */
    hbi_tensor k_view;
    hbi_tensor v_view;
    const void *k_ro = hbi_tensor_cdata(&page->k_tensor);
    const void *v_ro = hbi_tensor_cdata(&page->v_tensor);
    /* The KV cache is filled during the forward pass; we need mutable access.
     * The caller (runtime session) owns the KV backing memory and permits
     * mutation. Cast away const here is intentional and documented. */
    void *k_mut = (void *)(uintptr_t)k_ro; /* NOLINT: intentional, see above */
    void *v_mut = (void *)(uintptr_t)v_ro; /* NOLINT: intentional, see above */
    hbi_tensor_wrap(&k_view, page->k_tensor.dtype, &page->k_tensor.shape, k_mut,
                    hbi_tensor_nbytes(&page->k_tensor));
    hbi_tensor_wrap(&v_view, page->v_tensor.dtype, &page->v_tensor.shape, v_mut,
                    hbi_tensor_nbytes(&page->v_tensor));
    float *k_cache = (float *)hbi_tensor_data_mut(&k_view);
    float *v_cache = (float *)hbi_tensor_data_mut(&v_view);

    /* Allocate attention scores scratch (kv_len = pos + 1). */
    uint32_t kv_len = pos + 1u;
    float *scores =
        (float *)hbi_alloc(ctx->allocator, (size_t)kv_len * sizeof(float), 16, HBI_MEM_SCRATCH);
    if (!scores) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "gpt_oss forward: scores alloc");
    }

    /* ── Transformer blocks ─────────────────────────────────────────────── */
    for (uint32_t layer = 0; layer < ctx->num_layers; layer++) {
        const gpt_oss_layer_weights *lw = &ctx->layers[layer];
        const float *in_norm_w = (const float *)hbi_tensor_cdata(&lw->input_norm);
        const float *post_norm_w = (const float *)hbi_tensor_cdata(&lw->post_norm);
        const float *q_w = (const float *)hbi_tensor_cdata(&lw->q_proj);
        const float *k_w = (const float *)hbi_tensor_cdata(&lw->k_proj);
        const float *v_w = (const float *)hbi_tensor_cdata(&lw->v_proj);
        const float *o_w = (const float *)hbi_tensor_cdata(&lw->o_proj);
        const float *gate_w = (const float *)hbi_tensor_cdata(&lw->gate_proj);
        const float *up_w = (const float *)hbi_tensor_cdata(&lw->up_proj);
        const float *down_w = (const float *)hbi_tensor_cdata(&lw->down_proj);

        if (!in_norm_w || !post_norm_w || !q_w || !k_w || !v_w || !o_w || !gate_w || !up_w ||
            !down_w) {
            hbi_free(ctx->allocator, scores);
            return HBI_ERR_SETF(HBI_ERR_STATE, 0, "gpt_oss forward: layer %u has unbound weights",
                                layer);
        }

        /* 1. Pre-attention RMSNorm: scratch_b = norm(scratch_a) */
        ref_rmsnorm(ctx->scratch_a, in_norm_w, ctx->scratch_b, H, eps);

        /* 2. QKV projections. */
        ref_linear(ctx->scratch_b, q_w, ctx->attn_q, QH * HD, H);
        ref_linear(ctx->scratch_b, k_w, ctx->attn_k, KH * HD, H);
        ref_linear(ctx->scratch_b, v_w, ctx->attn_v, KH * HD, H);

        /* 3. RoPE for Q. */
        for (uint32_t h = 0; h < QH; h++) {
            ref_rope_head(ctx->attn_q + (size_t)h * HD, HD, pos, theta);
        }
        /* RoPE for K. */
        for (uint32_t h = 0; h < KH; h++) {
            ref_rope_head(ctx->attn_k + (size_t)h * HD, HD, pos, theta);
        }

        /* 4. Write K and V into the KV cache at position `pos`.
         * Cache layout: k_cache[pos, kv_head, head_dim], v_cache[pos, kv_head, head_dim] */
        size_t kv_pos_offset = (size_t)pos * KH * HD;
        memcpy(k_cache + kv_pos_offset, ctx->attn_k, (size_t)KH * HD * sizeof(float));
        memcpy(v_cache + kv_pos_offset, ctx->attn_v, (size_t)KH * HD * sizeof(float));

        /* 5. Causal attention over positions [0, pos]. */
        ref_attention(ctx->attn_q, k_cache, v_cache, ctx->attn_out, QH, KH, HD, kv_len, scores);

        /* 6. Output projection: scratch_b = attn_out * o_proj^T. */
        ref_linear(ctx->attn_out, o_w, ctx->scratch_b, H, QH * HD);

        /* 7. Residual connection: scratch_a += scratch_b. */
        for (uint32_t i = 0; i < H; i++) {
            ctx->scratch_a[i] += ctx->scratch_b[i];
        }

        /* 8. Pre-FFN RMSNorm: scratch_b = norm(scratch_a). */
        ref_rmsnorm(ctx->scratch_a, post_norm_w, ctx->scratch_b, H, eps);

        /* 9. FFN: SwiGLU.
         *   gate = gate_proj(scratch_b)  [IS]
         *   up   = up_proj(scratch_b)    [IS]
         *   hidden = silu(gate) * up     [IS] (element-wise)
         *   out  = down_proj(hidden)     [H] */
        ref_linear(ctx->scratch_b, gate_w, ctx->scratch_c, IS, H);
        ref_linear(ctx->scratch_b, up_w, ctx->scratch_d, IS, H);
        for (uint32_t i = 0; i < IS; i++) {
            ctx->scratch_c[i] = ref_silu(ctx->scratch_c[i]) * ctx->scratch_d[i];
        }
        ref_linear(ctx->scratch_c, down_w, ctx->scratch_b, H, IS);

        /* 10. Residual: scratch_a += scratch_b. */
        for (uint32_t i = 0; i < H; i++) {
            ctx->scratch_a[i] += ctx->scratch_b[i];
        }
    }

    hbi_free(ctx->allocator, scores);

    /* ── Final norm + lm_head ──────────────────────────────────────────── */
    const float *final_norm_w = (const float *)hbi_tensor_cdata(&ctx->final_norm);
    if (!final_norm_w) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "gpt_oss forward: final_norm not bound");
    }
    ref_rmsnorm(ctx->scratch_a, final_norm_w, ctx->scratch_b, H, eps);

    const float *lm_head_w = (const float *)hbi_tensor_cdata(&ctx->lm_head);
    if (!lm_head_w) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "gpt_oss forward: lm_head not bound");
    }
    ref_linear(ctx->scratch_b, lm_head_w, ctx->logits, ctx->vocab_size, H);

    *out_token = ref_argmax(ctx->logits, ctx->vocab_size);
    return HBI_OK;
}

/* ── Vtable implementation ────────────────────────────────────────────────── */

static hbi_status gpt_oss_init(const hbi_model_adapter *self, const hbi_load_session *session,
                               hbi_allocator *allocator) {
    HB_UNUSED(self);
    HB_UNUSED(allocator);
    memset(&g_gpt_oss_state, 0, sizeof(g_gpt_oss_state));
    memset(&g_gpt_oss_stats, 0, sizeof(g_gpt_oss_stats));
    if (!session) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "gpt_oss init: NULL session");
    }
    g_gpt_oss_state.rms_norm_eps = GPT_OSS_DEFAULT_EPS;
    g_gpt_oss_state.rope_theta = GPT_OSS_DEFAULT_ROPE_THETA;

    /* Override defaults from metadata if present. */
    const hbi_model_metadata *md = hbi_load_session_metadata(session);
    if (md) {
        const char *eps_str = hbi_model_metadata_get(md, "rms_norm_eps");
        const char *theta_str = hbi_model_metadata_get(md, "rope_theta");
        if (eps_str)
            parse_float(eps_str, &g_gpt_oss_state.rms_norm_eps);
        if (theta_str)
            parse_float(theta_str, &g_gpt_oss_state.rope_theta);
    }
    g_gpt_oss_state.initialized = true;
    return HBI_OK;
}

static hbi_status gpt_oss_validate_metadata(const hbi_model_adapter *self,
                                            const hbi_model_metadata *md) {
    HB_UNUSED(self);
    if (!md) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "gpt_oss validate: NULL metadata");
    }
    /* Check architecture string. */
    const char *arch = hbi_model_metadata_get(md, "architecture");
    if (!arch || strcmp(arch, GPT_OSS_NAME) != 0) {
        return HBI_ERR_SETF(HBI_ERR_NOT_FOUND, 0,
                            "gpt_oss validate: expected architecture='%s', got '%s'", GPT_OSS_NAME,
                            arch ? arch : "(null)");
    }
    /* All required numeric keys. */
    static const char *const k_required[] = {
        "hidden_size",       "intermediate_size", "num_attention_heads",     "num_key_value_heads",
        "num_hidden_layers", "vocab_size",        "max_position_embeddings",
    };
    for (size_t i = 0; i < HB_ARRAY_LEN(k_required); i++) {
        const char *val = hbi_model_metadata_get(md, k_required[i]);
        if (!val) {
            return HBI_ERR_SETF(HBI_ERR_NOT_FOUND, 0, "gpt_oss validate: missing required key '%s'",
                                k_required[i]);
        }
        uint32_t dummy;
        if (!parse_uint32(val, &dummy)) {
            return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0,
                                "gpt_oss validate: '%s' = '%s' is not a valid uint32",
                                k_required[i], val);
        }
        if (dummy == 0) {
            return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "gpt_oss validate: '%s' must be > 0",
                                k_required[i]);
        }
    }
    return HBI_OK;
}

static hbi_status gpt_oss_build_descriptor(const hbi_model_adapter *self,
                                           const hbi_model_metadata *md,
                                           hbi_model_descriptor *out) {
    HB_UNUSED(self);
    if (!md || !out) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "gpt_oss build_descriptor: NULL arg");
    }
    memset(out, 0, sizeof(*out));
    strncpy(out->architecture_name, GPT_OSS_NAME, HBI_ADAPTER_NAME_MAX - 1);
    out->architecture = HBI_ADAPTER_ARCH_TRANSFORMER_DENSE;

    uint32_t hidden = 0;
    uint32_t intermed = 0;
    uint32_t q_heads = 0;
    uint32_t kv_heads = 0;
    uint32_t layers = 0;
    uint32_t vocab = 0;
    uint32_t max_seq = 0;

    parse_uint32(hbi_model_metadata_get(md, "hidden_size"), &hidden);
    parse_uint32(hbi_model_metadata_get(md, "intermediate_size"), &intermed);
    parse_uint32(hbi_model_metadata_get(md, "num_attention_heads"), &q_heads);
    parse_uint32(hbi_model_metadata_get(md, "num_key_value_heads"), &kv_heads);
    parse_uint32(hbi_model_metadata_get(md, "num_hidden_layers"), &layers);
    parse_uint32(hbi_model_metadata_get(md, "vocab_size"), &vocab);
    parse_uint32(hbi_model_metadata_get(md, "max_position_embeddings"), &max_seq);

    if (layers > HBI_GPT_OSS_MAX_LAYERS) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "gpt_oss build_descriptor: num_layers %u > max %u",
                            layers, HBI_GPT_OSS_MAX_LAYERS);
    }

    out->hidden_size = hidden;
    out->intermediate_size = intermed;
    out->num_attention_heads = q_heads;
    out->num_kv_heads = (kv_heads > 0) ? kv_heads : q_heads;
    out->num_layers = layers;
    out->vocab_size = vocab;
    out->max_seq_length = max_seq;

    /* MoE fields (0 = dense). */
    out->num_experts = 0;
    out->num_experts_active = 0;
    out->moe_layer_stride = 0;

    /* Variants. */
    out->attention_variant = (out->num_kv_heads < q_heads) ? HBI_ATTENTION_GQA : HBI_ATTENTION_MHA;
    out->norm_type = HBI_NORM_RMSNORM;
    out->activation = HBI_ADAPTER_ACT_SWIGLU;

    /* Layer-type mask. */
    hbi_descriptor_set_layer(out, HBI_LAYER_EMBEDDING);
    hbi_descriptor_set_layer(out, HBI_LAYER_NORMALIZATION);
    hbi_descriptor_set_layer(out, HBI_LAYER_ATTENTION);
    hbi_descriptor_set_layer(out, HBI_LAYER_FEED_FORWARD);
    hbi_descriptor_set_layer(out, HBI_LAYER_RESIDUAL);
    hbi_descriptor_set_layer(out, HBI_LAYER_OUTPUT_HEAD);

    return HBI_OK;
}

/* ── Graph construction ───────────────────────────────────────────────────── */

/* Build a structural graph for one transformer layer.
 * The graph represents the data flow; the forward pass logic is separate.
 * Node naming: "l{layer}.{block}" for diagnostics. */
static hbi_status build_layer_graph(hbi_graph_builder *b, uint32_t layer, uint32_t in_id,
                                    uint32_t *out_id, uint32_t H, uint32_t QHD, uint32_t KVHD,
                                    uint32_t IS) {
    hbi_kernel_params p;
    memset(&p, 0, sizeof(p));
    char name[64];
    uint32_t tmp[4];

    hbi_shape vec_H = {.rank = 1, .dims = {H}};
    hbi_shape vec_QHD = {.rank = 1, .dims = {QHD}};
    hbi_shape vec_IS = {.rank = 1, .dims = {IS}};
    HB_UNUSED(vec_QHD);

    /* pre_norm */
    snprintf(name, sizeof(name), "l%u.pre_norm", layer);
    hbi_status st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_RMSNORM, &p, &in_id, 1, &tmp[0], 1);
    if (st != HBI_OK)
        return st;

    /* q_proj, k_proj, v_proj (matmul — input is tmp[0]) */
    uint32_t q_out, k_out, v_out;
    snprintf(name, sizeof(name), "l%u.q_proj", layer);
    st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_MATMUL, &p, &tmp[0], 1, &q_out, 1);
    if (st != HBI_OK)
        return st;

    snprintf(name, sizeof(name), "l%u.k_proj", layer);
    st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_MATMUL, &p, &tmp[0], 1, &k_out, 1);
    if (st != HBI_OK)
        return st;

    snprintf(name, sizeof(name), "l%u.v_proj", layer);
    st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_MATMUL, &p, &tmp[0], 1, &v_out, 1);
    if (st != HBI_OK)
        return st;

    /* rope */
    uint32_t q_rope, k_rope;
    snprintf(name, sizeof(name), "l%u.rope_q", layer);
    st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_ROPE, &p, &q_out, 1, &q_rope, 1);
    if (st != HBI_OK)
        return st;

    snprintf(name, sizeof(name), "l%u.rope_k", layer);
    st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_ROPE, &p, &k_out, 1, &k_rope, 1);
    if (st != HBI_OK)
        return st;

    /* attention */
    uint32_t attn_ins[3] = {q_rope, k_rope, v_out};
    uint32_t attn_out;
    snprintf(name, sizeof(name), "l%u.attn", layer);
    st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_ATTENTION, &p, attn_ins, 3, &attn_out, 1);
    if (st != HBI_OK)
        return st;

    /* o_proj */
    uint32_t o_out;
    snprintf(name, sizeof(name), "l%u.o_proj", layer);
    st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_MATMUL, &p, &attn_out, 1, &o_out, 1);
    if (st != HBI_OK)
        return st;

    /* residual: in_id + o_out */
    uint32_t res1_ins[2] = {in_id, o_out};
    uint32_t res1_out;
    p.u.elementwise = HBI_ELEMENTWISE_ADD;
    snprintf(name, sizeof(name), "l%u.res1", layer);
    st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_ELEMENTWISE, &p, res1_ins, 2, &res1_out, 1);
    if (st != HBI_OK)
        return st;

    /* post_norm */
    memset(&p, 0, sizeof(p));
    uint32_t pnorm_out;
    snprintf(name, sizeof(name), "l%u.post_norm", layer);
    st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_RMSNORM, &p, &res1_out, 1, &pnorm_out, 1);
    if (st != HBI_OK)
        return st;

    /* gate_proj, up_proj */
    uint32_t gate_out, up_out;
    snprintf(name, sizeof(name), "l%u.gate_proj", layer);
    st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_MATMUL, &p, &pnorm_out, 1, &gate_out, 1);
    if (st != HBI_OK)
        return st;

    snprintf(name, sizeof(name), "l%u.up_proj", layer);
    st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_MATMUL, &p, &pnorm_out, 1, &up_out, 1);
    if (st != HBI_OK)
        return st;

    /* SiLU activation on gate */
    uint32_t silu_out;
    p.u.activation = HBI_ACTIVATION_SILU;
    snprintf(name, sizeof(name), "l%u.silu", layer);
    st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_ACTIVATION, &p, &gate_out, 1, &silu_out, 1);
    if (st != HBI_OK)
        return st;

    /* gate * up (SwiGLU) */
    uint32_t swiglu_ins[2] = {silu_out, up_out};
    uint32_t swiglu_out;
    memset(&p, 0, sizeof(p));
    p.u.elementwise = HBI_ELEMENTWISE_MUL;
    snprintf(name, sizeof(name), "l%u.swiglu", layer);
    st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_ELEMENTWISE, &p, swiglu_ins, 2, &swiglu_out, 1);
    if (st != HBI_OK)
        return st;

    /* down_proj */
    uint32_t down_out;
    memset(&p, 0, sizeof(p));
    snprintf(name, sizeof(name), "l%u.down_proj", layer);
    st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_MATMUL, &p, &swiglu_out, 1, &down_out, 1);
    if (st != HBI_OK)
        return st;

    /* residual: res1_out + down_out */
    uint32_t res2_ins[2] = {res1_out, down_out};
    uint32_t res2_out;
    p.u.elementwise = HBI_ELEMENTWISE_ADD;
    snprintf(name, sizeof(name), "l%u.res2", layer);
    st = hbi_graph_add_node(b, name, HBI_KERNEL_OP_ELEMENTWISE, &p, res2_ins, 2, out_id, 1);
    if (st != HBI_OK)
        return st;

    HB_UNUSED(vec_H);
    HB_UNUSED(vec_IS);
    HB_UNUSED(res2_out);
    HB_UNUSED(KVHD);
    return HBI_OK;
}

static hbi_status gpt_oss_build_graph(const hbi_model_adapter *self, hbi_graph_builder *b,
                                      const hbi_model_descriptor *desc) {
    HB_UNUSED(self);
    if (!b || !desc) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "gpt_oss build_graph: NULL arg");
    }

    uint64_t t0 = hbi_time_monotonic_ns();

    uint32_t H = desc->hidden_size;
    uint32_t QH = desc->num_attention_heads;
    uint32_t KH = desc->num_kv_heads;
    uint32_t HD = H / QH;
    uint32_t QHD = QH * HD;
    uint32_t KHD = KH * HD;
    uint32_t IS = desc->intermediate_size;
    uint32_t L = desc->num_layers;

    /* Input: token_ids — shape [1] (single token decode). */
    hbi_shape token_shape = {.rank = 1, .dims = {1}};
    uint32_t tok_id;
    hbi_status st = hbi_graph_add_input(b, "token_ids", &token_shape, HBI_DTYPE_INT8, &tok_id);
    if (st != HBI_OK)
        return st;

    /* Embedding lookup → hidden vector [H] (represented as COPY for now —
     * a dedicated EMBEDDING op lands in M2). The embedding weight is a
     * constant that gets bound at runtime. */
    hbi_kernel_params p;
    memset(&p, 0, sizeof(p));
    hbi_shape h_shape = {.rank = 1, .dims = {H}};
    hbi_shape qhd_shape = {.rank = 1, .dims = {QHD}};
    hbi_shape khd_shape = {.rank = 1, .dims = {KHD}};
    hbi_shape is_shape = {.rank = 1, .dims = {IS}};
    HB_UNUSED(qhd_shape);
    HB_UNUSED(khd_shape);
    HB_UNUSED(is_shape);

    /* Embed: emit a COPY node that "copies" the token embedding row.
     * At runtime the actual lookup happens imperatively. */
    uint32_t embed_out;
    st = hbi_graph_add_input(b, "hidden", &h_shape, HBI_DTYPE_FP32, &embed_out);
    if (st != HBI_OK)
        return st;

    /* Build N transformer layer sub-graphs. */
    uint32_t cur = embed_out;
    for (uint32_t layer = 0; layer < L; layer++) {
        uint32_t next;
        st = build_layer_graph(b, layer, cur, &next, H, QHD, KHD, IS);
        if (st != HBI_OK)
            return st;
        cur = next;
    }

    /* Final norm + lm_head. */
    uint32_t fnorm_out;
    st = hbi_graph_add_node(b, "final_norm", HBI_KERNEL_OP_RMSNORM, &p, &cur, 1, &fnorm_out, 1);
    if (st != HBI_OK)
        return st;

    uint32_t logits_out;
    st = hbi_graph_add_node(b, "lm_head", HBI_KERNEL_OP_MATMUL, &p, &fnorm_out, 1, &logits_out, 1);
    if (st != HBI_OK)
        return st;

    g_gpt_oss_stats.graph_build_time_ns = hbi_time_monotonic_ns() - t0;

    /* Approximate graph metrics (counted outside the builder which is opaque). */
    g_gpt_oss_stats.graph_nodes =
        2 +
        L * 14u; /* embed + N*(pre_norm,qkv,rope,attn,o,res1,pnorm,gate,up,silu,swiglu,down,res2) +
                    final_norm + lm_head */
    g_gpt_oss_stats.graph_values = g_gpt_oss_stats.graph_nodes + 2u; /* approx */

    return HBI_OK;
}

/* ── Tensor registration ──────────────────────────────────────────────────── */

/* Format layer tensor name into buf. */
static void layer_tensor_name(char *buf, size_t bufsz, uint32_t layer, const char *suffix) {
    snprintf(buf, bufsz, "model.layers.%u.%s", layer, suffix);
}

static hbi_status require_tensor(const hbi_model_manifest *m, const char *name,
                                 const hbi_tensor_entry **out) {
    const hbi_tensor_entry *e = hbi_model_manifest_find(m, name);
    if (!e) {
        return HBI_ERR_SETF(HBI_ERR_NOT_FOUND, 0, "gpt_oss: missing tensor '%s'", name);
    }
    if (out)
        *out = e;
    return HBI_OK;
}

static hbi_status check_shape_2d(const hbi_tensor_entry *e, int64_t rows, int64_t cols) {
    if (e->shape.rank != 2) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "gpt_oss: tensor '%s' expected rank 2, got %u",
                            e->name, e->shape.rank);
    }
    if (e->shape.dims[0] != rows || e->shape.dims[1] != cols) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0,
                            "gpt_oss: tensor '%s' shape [%lld,%lld] != expected [%lld,%lld]",
                            e->name, (long long)e->shape.dims[0], (long long)e->shape.dims[1],
                            (long long)rows, (long long)cols);
    }
    return HBI_OK;
}

static hbi_status check_shape_1d(const hbi_tensor_entry *e, int64_t n) {
    if (e->shape.rank != 1) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "gpt_oss: tensor '%s' expected rank 1, got %u",
                            e->name, e->shape.rank);
    }
    if (e->shape.dims[0] != n) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "gpt_oss: tensor '%s' dim[0]=%lld != expected %lld",
                            e->name, (long long)e->shape.dims[0], (long long)n);
    }
    return HBI_OK;
}

static hbi_status gpt_oss_register_tensors(const hbi_model_adapter *self,
                                           const hbi_model_manifest *m,
                                           const hbi_model_descriptor *desc) {
    HB_UNUSED(self);
    if (!m || !desc) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "gpt_oss register_tensors: NULL arg");
    }

    uint32_t H = desc->hidden_size;
    uint32_t QH = desc->num_attention_heads;
    uint32_t KH = desc->num_kv_heads;
    uint32_t HD = H / QH;
    uint32_t IS = desc->intermediate_size;
    uint32_t V = desc->vocab_size;
    uint32_t L = desc->num_layers;

    /* Global tensors. */
    const hbi_tensor_entry *e = NULL;
    hbi_status st;

    st = require_tensor(m, "model.embed_tokens.weight", &e);
    if (st != HBI_OK)
        return st;
    st = check_shape_2d(e, (int64_t)V, (int64_t)H);
    if (st != HBI_OK)
        return st;

    st = require_tensor(m, "model.norm.weight", &e);
    if (st != HBI_OK)
        return st;
    st = check_shape_1d(e, (int64_t)H);
    if (st != HBI_OK)
        return st;

    st = require_tensor(m, "lm_head.weight", &e);
    if (st != HBI_OK)
        return st;
    st = check_shape_2d(e, (int64_t)V, (int64_t)H);
    if (st != HBI_OK)
        return st;

    /* Per-layer tensors. */
    char name[HBI_TENSOR_NAME_MAX];
    for (uint32_t layer = 0; layer < L; layer++) {
        layer_tensor_name(name, sizeof(name), layer, "input_layernorm.weight");
        st = require_tensor(m, name, &e);
        if (st != HBI_OK)
            return st;
        st = check_shape_1d(e, (int64_t)H);
        if (st != HBI_OK)
            return st;

        layer_tensor_name(name, sizeof(name), layer, "post_attention_layernorm.weight");
        st = require_tensor(m, name, &e);
        if (st != HBI_OK)
            return st;
        st = check_shape_1d(e, (int64_t)H);
        if (st != HBI_OK)
            return st;

        layer_tensor_name(name, sizeof(name), layer, "self_attn.q_proj.weight");
        st = require_tensor(m, name, &e);
        if (st != HBI_OK)
            return st;
        st = check_shape_2d(e, (int64_t)(QH * HD), (int64_t)H);
        if (st != HBI_OK)
            return st;

        layer_tensor_name(name, sizeof(name), layer, "self_attn.k_proj.weight");
        st = require_tensor(m, name, &e);
        if (st != HBI_OK)
            return st;
        st = check_shape_2d(e, (int64_t)(KH * HD), (int64_t)H);
        if (st != HBI_OK)
            return st;

        layer_tensor_name(name, sizeof(name), layer, "self_attn.v_proj.weight");
        st = require_tensor(m, name, &e);
        if (st != HBI_OK)
            return st;
        st = check_shape_2d(e, (int64_t)(KH * HD), (int64_t)H);
        if (st != HBI_OK)
            return st;

        layer_tensor_name(name, sizeof(name), layer, "self_attn.o_proj.weight");
        st = require_tensor(m, name, &e);
        if (st != HBI_OK)
            return st;
        st = check_shape_2d(e, (int64_t)H, (int64_t)(QH * HD));
        if (st != HBI_OK)
            return st;

        layer_tensor_name(name, sizeof(name), layer, "mlp.gate_proj.weight");
        st = require_tensor(m, name, &e);
        if (st != HBI_OK)
            return st;
        st = check_shape_2d(e, (int64_t)IS, (int64_t)H);
        if (st != HBI_OK)
            return st;

        layer_tensor_name(name, sizeof(name), layer, "mlp.up_proj.weight");
        st = require_tensor(m, name, &e);
        if (st != HBI_OK)
            return st;
        st = check_shape_2d(e, (int64_t)IS, (int64_t)H);
        if (st != HBI_OK)
            return st;

        layer_tensor_name(name, sizeof(name), layer, "mlp.down_proj.weight");
        st = require_tensor(m, name, &e);
        if (st != HBI_OK)
            return st;
        st = check_shape_2d(e, (int64_t)H, (int64_t)IS);
        if (st != HBI_OK)
            return st;
    }

    g_gpt_oss_stats.tensors_registered = hbi_model_manifest_count(m);
    return HBI_OK;
}

/* ── Context lifecycle ────────────────────────────────────────────────────── */

static hbi_status gpt_oss_create_context(const hbi_model_adapter *self,
                                         const hbi_model_descriptor *desc, hbi_allocator *allocator,
                                         hbi_model_context **out_ctx) {
    HB_UNUSED(self);
    if (!desc || !allocator || !out_ctx) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "gpt_oss create_context: NULL arg");
    }
    *out_ctx = NULL;

    /* Allocate the outer framework context. */
    hbi_model_context *fctx =
        (hbi_model_context *)hbi_alloc(allocator, sizeof(hbi_model_context), 8, HBI_MEM_GENERAL);
    if (!fctx)
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "gpt_oss create_context: outer alloc");
    memset(fctx, 0, sizeof(*fctx));
    fctx->allocator = allocator;
    fctx->adapter = self;
    fctx->descriptor = *desc;

    /* Allocate GPT-OSS private context. */
    gpt_oss_context *ctx =
        (gpt_oss_context *)hbi_alloc(allocator, sizeof(gpt_oss_context), 8, HBI_MEM_GENERAL);
    if (!ctx) {
        hbi_free(allocator, fctx);
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "gpt_oss create_context: private alloc");
    }
    memset(ctx, 0, sizeof(*ctx));

    ctx->allocator = allocator;
    ctx->descriptor = desc;
    ctx->hidden_size = desc->hidden_size;
    ctx->num_q_heads = desc->num_attention_heads;
    ctx->num_kv_heads = desc->num_kv_heads;
    ctx->head_dim = desc->hidden_size / desc->num_attention_heads;
    ctx->intermediate_size = desc->intermediate_size;
    ctx->vocab_size = desc->vocab_size;
    ctx->rms_norm_eps = g_gpt_oss_state.rms_norm_eps;
    ctx->rope_theta = g_gpt_oss_state.rope_theta;
    ctx->num_layers = desc->num_layers;

    uint32_t H = ctx->hidden_size;
    uint32_t QH = ctx->num_q_heads;
    uint32_t KH = ctx->num_kv_heads;
    uint32_t HD = ctx->head_dim;
    uint32_t IS = ctx->intermediate_size;
    uint32_t V = ctx->vocab_size;

    /* Allocate scratch buffers. */
#define ALLOC_SCRATCH(field, nelems)                                                               \
    ctx->field =                                                                                   \
        (float *)hbi_alloc(allocator, (size_t)(nelems) * sizeof(float), 64, HBI_MEM_SCRATCH);      \
    if (!ctx->field) {                                                                             \
        goto oom;                                                                                  \
    }

    ALLOC_SCRATCH(scratch_a, H);
    ALLOC_SCRATCH(scratch_b, H);
    ALLOC_SCRATCH(scratch_c, IS);
    ALLOC_SCRATCH(scratch_d, IS);
    ALLOC_SCRATCH(attn_q, QH * HD);
    ALLOC_SCRATCH(attn_k, KH * HD);
    ALLOC_SCRATCH(attn_v, KH * HD);
    ALLOC_SCRATCH(attn_out, QH * HD);
    ALLOC_SCRATCH(logits, V);
#undef ALLOC_SCRATCH

    /* Allocate per-layer weight structs. */
    ctx->layers = (gpt_oss_layer_weights *)hbi_alloc(
        allocator, (size_t)desc->num_layers * sizeof(gpt_oss_layer_weights), 8, HBI_MEM_GENERAL);
    if (!ctx->layers)
        goto oom;
    memset(ctx->layers, 0, (size_t)desc->num_layers * sizeof(gpt_oss_layer_weights));

    /* Create KV manager and context. */
    hbi_status st = hbi_kv_manager_create(allocator, NULL, &ctx->kv_manager);
    if (st != HBI_OK)
        goto cleanup;

    /* KV shape: [max_seq, num_kv_heads, head_dim] — collapsed to 1D for the page. */
    uint32_t max_seq = desc->max_seq_length;
    hbi_shape kv_shape = {.rank = 3, .dims = {(int64_t)max_seq, (int64_t)KH, (int64_t)HD}};

    st = hbi_kv_context_create(ctx->kv_manager, max_seq, HBI_DTYPE_FP32, &kv_shape, &kv_shape,
                               &ctx->kv_handle);
    if (st != HBI_OK)
        goto cleanup;

    ctx->initialized = true;
    fctx->private_data = ctx;
    fctx->private_data_size = sizeof(gpt_oss_context);
    fctx->stats = g_gpt_oss_stats;
    fctx->initialized = true;

    g_gpt_oss_stats.adapter_memory_bytes +=
        sizeof(gpt_oss_context) +
        (size_t)(H + H + IS + IS + QH * HD + KH * HD + KH * HD + QH * HD + V) * sizeof(float) +
        (size_t)desc->num_layers * sizeof(gpt_oss_layer_weights);

    *out_ctx = fctx;
    return HBI_OK;

oom:
    st = HBI_ERR_SET(HBI_ERR_OOM, 0, "gpt_oss create_context: scratch alloc");
cleanup:
    /* Partial free — scratch buffers are freed by hbi_free (pointer may be NULL, safe). */
    hbi_free(allocator, ctx->scratch_a);
    hbi_free(allocator, ctx->scratch_b);
    hbi_free(allocator, ctx->scratch_c);
    hbi_free(allocator, ctx->scratch_d);
    hbi_free(allocator, ctx->attn_q);
    hbi_free(allocator, ctx->attn_k);
    hbi_free(allocator, ctx->attn_v);
    hbi_free(allocator, ctx->attn_out);
    hbi_free(allocator, ctx->logits);
    hbi_free(allocator, ctx->layers);
    if (ctx->kv_handle && ctx->kv_manager)
        hbi_kv_context_destroy(ctx->kv_manager, ctx->kv_handle);
    if (ctx->kv_manager)
        hbi_kv_manager_destroy(ctx->kv_manager);
    hbi_free(allocator, ctx);
    hbi_free(allocator, fctx);
    return st;
}

static void gpt_oss_destroy_context(const hbi_model_adapter *self, hbi_model_context *fctx) {
    HB_UNUSED(self);
    if (!fctx)
        return;
    gpt_oss_context *ctx = (gpt_oss_context *)fctx->private_data;
    if (ctx) {
        hbi_allocator *a = ctx->allocator;
        hbi_free(a, ctx->scratch_a);
        hbi_free(a, ctx->scratch_b);
        hbi_free(a, ctx->scratch_c);
        hbi_free(a, ctx->scratch_d);
        hbi_free(a, ctx->attn_q);
        hbi_free(a, ctx->attn_k);
        hbi_free(a, ctx->attn_v);
        hbi_free(a, ctx->attn_out);
        hbi_free(a, ctx->logits);
        hbi_free(a, ctx->layers);
        if (ctx->kv_handle && ctx->kv_manager)
            hbi_kv_context_destroy(ctx->kv_manager, ctx->kv_handle);
        if (ctx->kv_manager)
            hbi_kv_manager_destroy(ctx->kv_manager);
        hbi_free(a, ctx);
    }
    hbi_free(fctx->allocator, fctx);
}

/* ── Capabilities / statistics ────────────────────────────────────────────── */

static uint32_t gpt_oss_get_capabilities(const hbi_model_adapter *self) {
    HB_UNUSED(self);
    return HBI_CAP_SUPPORTS_BATCHED_INFERENCE | HBI_CAP_SUPPORTS_QUANTIZED_WEIGHTS |
           HBI_CAP_SUPPORTS_LONG_CONTEXT;
}

static hbi_status gpt_oss_get_statistics(const hbi_model_adapter *self, hbi_model_statistics *out) {
    HB_UNUSED(self);
    if (!out)
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "gpt_oss get_statistics: NULL out");
    *out = g_gpt_oss_stats;
    return HBI_OK;
}

static void gpt_oss_shutdown(const hbi_model_adapter *self) {
    HB_UNUSED(self);
    memset(&g_gpt_oss_state, 0, sizeof(g_gpt_oss_state));
    memset(&g_gpt_oss_stats, 0, sizeof(g_gpt_oss_stats));
}

/* ── Static adapter instance ──────────────────────────────────────────────── */

static const hbi_model_adapter g_gpt_oss_adapter = {
    .name = GPT_OSS_NAME,
    .architecture = HBI_ADAPTER_ARCH_TRANSFORMER_DENSE,
    .init = gpt_oss_init,
    .validate_metadata = gpt_oss_validate_metadata,
    .build_descriptor = gpt_oss_build_descriptor,
    .build_graph = gpt_oss_build_graph,
    .register_tensors = gpt_oss_register_tensors,
    .create_context = gpt_oss_create_context,
    .destroy_context = gpt_oss_destroy_context,
    .get_capabilities = gpt_oss_get_capabilities,
    .get_statistics = gpt_oss_get_statistics,
    .shutdown = gpt_oss_shutdown,
};

/* ── Public registration ──────────────────────────────────────────────────── */

hbi_status hbi_adapter_gpt_oss_register(void) {
    return hbi_adapter_register(&g_gpt_oss_adapter);
}

const hbi_model_adapter *hbi_adapter_gpt_oss_get(void) {
    return &g_gpt_oss_adapter;
}

/* ── Weight binding helper (used by integration tests and tools) ───────────── */

/* Bind pre-loaded weight data for a model context.
 * `data` must remain valid for the lifetime of the context.
 * `tensor` is initialized as a borrowed view into `data`. */
static hbi_status bind_weight_tensor(hbi_tensor *t, const void *data, hbi_dtype dtype,
                                     const hbi_shape *shape) {
    int64_t n_elems = 0;
    hbi_status st = hbi_shape_elem_count(shape, &n_elems);
    if (st != HBI_OK)
        return st;
    size_t nbytes = (size_t)n_elems * ((size_t)hbi_dtype_bits(dtype) / 8u);
    return hbi_tensor_wrap_readonly(t, dtype, shape, data, nbytes);
}

/* hbi_adapter_gpt_oss_bind_weights — bind raw weight buffers into a model context.
 *
 * This is the bridge between the model loader (which provides raw bytes from the
 * model file) and the forward pass (which needs hbi_tensor views into those bytes).
 *
 * The caller reads weight data from the load session (via format handler
 * read_tensor_data) and calls this function to register each tensor. The adapter
 * then wraps the buffer into a borrowed hbi_tensor so the forward pass can access
 * it without copying.
 *
 * `name` is the GPT-OSS tensor name (e.g. "model.layers.0.self_attn.q_proj.weight").
 * `data` is the raw fp32 weight buffer.
 * `shape` is the tensor shape as declared in the manifest.
 */
hbi_status hbi_adapter_gpt_oss_bind_weights(hbi_model_context *fctx, const char *name,
                                            const void *data, const hbi_shape *shape) {
    if (!fctx || !name || !data || !shape) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "gpt_oss bind_weights: NULL arg");
    }
    gpt_oss_context *ctx = (gpt_oss_context *)fctx->private_data;
    if (!ctx || !ctx->initialized) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "gpt_oss bind_weights: context not initialized");
    }

    hbi_status st;

    /* Global tensors. */
    if (strcmp(name, "model.embed_tokens.weight") == 0) {
        return bind_weight_tensor(&ctx->embed_tokens, data, HBI_DTYPE_FP32, shape);
    }
    if (strcmp(name, "model.norm.weight") == 0) {
        return bind_weight_tensor(&ctx->final_norm, data, HBI_DTYPE_FP32, shape);
    }
    if (strcmp(name, "lm_head.weight") == 0) {
        return bind_weight_tensor(&ctx->lm_head, data, HBI_DTYPE_FP32, shape);
    }

    /* Parse layer index from "model.layers.{i}.{suffix}". */
    uint32_t layer_idx = 0;
    const char *layers_prefix = "model.layers.";
    if (strncmp(name, layers_prefix, strlen(layers_prefix)) != 0) {
        return HBI_ERR_SETF(HBI_ERR_NOT_FOUND, 0, "gpt_oss bind_weights: unrecognized tensor '%s'",
                            name);
    }

    const char *rest = name + strlen(layers_prefix);
    char *end_ptr = NULL;
    long layer_num = strtol(rest, &end_ptr, 10);
    if (!end_ptr || *end_ptr != '.' || layer_num < 0) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "gpt_oss bind_weights: bad layer index in '%s'",
                            name);
    }
    layer_idx = (uint32_t)layer_num;
    if (layer_idx >= ctx->num_layers) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0,
                            "gpt_oss bind_weights: layer %u >= num_layers %u", layer_idx,
                            ctx->num_layers);
    }

    gpt_oss_layer_weights *lw = &ctx->layers[layer_idx];
    const char *suffix = end_ptr + 1; /* skip the '.' */

    if (strcmp(suffix, "input_layernorm.weight") == 0)
        return bind_weight_tensor(&lw->input_norm, data, HBI_DTYPE_FP32, shape);
    if (strcmp(suffix, "post_attention_layernorm.weight") == 0)
        return bind_weight_tensor(&lw->post_norm, data, HBI_DTYPE_FP32, shape);
    if (strcmp(suffix, "self_attn.q_proj.weight") == 0)
        return bind_weight_tensor(&lw->q_proj, data, HBI_DTYPE_FP32, shape);
    if (strcmp(suffix, "self_attn.k_proj.weight") == 0)
        return bind_weight_tensor(&lw->k_proj, data, HBI_DTYPE_FP32, shape);
    if (strcmp(suffix, "self_attn.v_proj.weight") == 0)
        return bind_weight_tensor(&lw->v_proj, data, HBI_DTYPE_FP32, shape);
    if (strcmp(suffix, "self_attn.o_proj.weight") == 0)
        return bind_weight_tensor(&lw->o_proj, data, HBI_DTYPE_FP32, shape);
    if (strcmp(suffix, "mlp.gate_proj.weight") == 0)
        return bind_weight_tensor(&lw->gate_proj, data, HBI_DTYPE_FP32, shape);
    if (strcmp(suffix, "mlp.up_proj.weight") == 0)
        return bind_weight_tensor(&lw->up_proj, data, HBI_DTYPE_FP32, shape);
    if (strcmp(suffix, "mlp.down_proj.weight") == 0)
        return bind_weight_tensor(&lw->down_proj, data, HBI_DTYPE_FP32, shape);

    return HBI_ERR_SETF(HBI_ERR_NOT_FOUND, 0,
                        "gpt_oss bind_weights: unrecognized suffix '%s' in '%s'", suffix, name);

    HB_UNUSED(st);
}

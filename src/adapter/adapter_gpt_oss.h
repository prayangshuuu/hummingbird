/* adapter_gpt_oss.h — GPT-OSS Model Adapter (RFC-015).
 *
 * Public header for the GPT-OSS adapter. Defines registration and constants
 * for the GPT-OSS family (dense-transformer, RMSNorm, SwiGLU, GQA, RoPE).
 *
 * Callers register the adapter once at engine init:
 *
 *   hbi_adapter_gpt_oss_register();
 *
 * The adapter resolves for any model whose "architecture" metadata key equals
 * "gpt_oss". Exact tensor-naming conventions and geometry parsing are
 * documented in docs/architecture/14-gpt-oss-integration.md.
 *
 * Supported variants:
 *   - GPT-OSS 20B  (dense,  GQA, 40 layers)
 *   - GPT-OSS 120B (dense,  GQA, 96 layers)
 *   Future: GPT-OSS MoE variants when moe_layer_stride > 0 in metadata.
 *
 * ── Tensor naming (GPT-OSS SafeTensors / GGUF convention) ───────────────
 * model.embed_tokens.weight                      [vocab_size, hidden_size]
 * model.layers.{i}.input_layernorm.weight        [hidden_size]
 * model.layers.{i}.self_attn.q_proj.weight       [num_q_heads*head_dim, hidden_size]
 * model.layers.{i}.self_attn.k_proj.weight       [num_kv_heads*head_dim, hidden_size]
 * model.layers.{i}.self_attn.v_proj.weight       [num_kv_heads*head_dim, hidden_size]
 * model.layers.{i}.self_attn.o_proj.weight       [hidden_size, num_q_heads*head_dim]
 * model.layers.{i}.post_attention_layernorm.weight [hidden_size]
 * model.layers.{i}.mlp.gate_proj.weight          [intermediate_size, hidden_size]
 * model.layers.{i}.mlp.up_proj.weight            [intermediate_size, hidden_size]
 * model.layers.{i}.mlp.down_proj.weight          [hidden_size, intermediate_size]
 * model.norm.weight                              [hidden_size]
 * lm_head.weight                                 [vocab_size, hidden_size]
 *
 * ── Required metadata keys ───────────────────────────────────────────────
 * "architecture"       = "gpt_oss"
 * "hidden_size"        = integer string
 * "intermediate_size"  = integer string
 * "num_attention_heads"= integer string  (Q heads)
 * "num_key_value_heads"= integer string  (KV heads; == Q for MHA)
 * "num_hidden_layers"  = integer string
 * "vocab_size"         = integer string
 * "max_position_embeddings" = integer string
 * "rms_norm_eps"       = float string    (e.g. "1e-05"; optional, default 1e-5)
 * "rope_theta"         = float string    (e.g. "10000.0"; optional, default 10000)
 */
#ifndef HB_ADAPTER_GPT_OSS_H
#define HB_ADAPTER_GPT_OSS_H

#include "adapter/adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Architecture identifier string written in model metadata. */
#define HBI_GPT_OSS_ARCH_NAME "gpt_oss"

/* Maximum number of transformer layers supported. */
#define HBI_GPT_OSS_MAX_LAYERS 256u

/* Register the GPT-OSS adapter into the global adapter registry.
 * Must be called at engine init before hbi_model_load().
 * Fails HBI_ERR_STATE if a "gpt_oss" adapter is already registered. */
hbi_status hbi_adapter_gpt_oss_register(void);

/* Direct access to the static adapter instance (for tests). */
const hbi_model_adapter *hbi_adapter_gpt_oss_get(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_ADAPTER_GPT_OSS_H */

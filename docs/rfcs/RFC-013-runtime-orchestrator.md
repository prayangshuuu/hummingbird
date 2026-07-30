RFC-013: Runtime Orchestrator — Forward Loop, Layer Sequencing, and Logit Production

Status: DRAFT — Implementation Milestone: M2
Authors: Hummingbird Core Team

---

## 1. Motivation

RFC-011 defines the model loader. RFC-012 defines KV cache management.
RFC-014 defines the adapter framework (which builds execution graphs and
dispatches forward passes through `hbi_adapter_gpt_oss_forward()`).

What is missing is a top-level **orchestrator** that:
- Owns the decoding loop (greedy, sampling, beam search)
- Drives the streaming engine and KV manager in concert
- Sequences layers by calling the adapter's forward pass
- Converts the adapter's output logit tensor into a next-token ID
- Returns a token stream to the caller

This document specifies the `runtime` module (layer 9), which is the
single entry point for end-to-end inference.

---

## 2. Scope

In scope:
- Greedy decoding loop
- Token-by-token generation state machine
- Integration point for all lower layers (adapter, scheduler, streaming,
  KV manager, model session)
- Stop-condition checking (EOS token, max_tokens, stop strings)
- Session-level statistics (token/s, TTFT, memory usage)

Out of scope (future RFCs):
- Sampler strategies beyond greedy (top-k, top-p, temperature, min-p)
- Speculative decoding
- Batch inference (handled at a higher level)
- Streaming HTTP responses (frontend concern)

---

## 3. Public Internal API

The runtime module is internal (`hbi_` prefix). The public interface
(`hb_` prefix in `hummingbird.h`) is specified in a separate milestone.

```c
/* runtime.h additions (M2) */

typedef struct hbi_runtime_config {
    uint32_t max_new_tokens;  /* hard cap on generated tokens */
    uint32_t eos_token_id;    /* stop on this token ID (0 = disabled) */
    bool greedy;              /* greedy sampling (argmax logits) */
} hbi_runtime_config;

typedef struct hbi_runtime_session hbi_runtime_session;

/* Create a runtime session from a completed load session + resolved adapter.
 * The load session must outlive the runtime session.
 * Returns HBI_ERR_INVALID_ARG if any arg is NULL.
 * Returns HBI_ERR_OOM if memory cannot be allocated. */
hbi_status hbi_runtime_session_create(const hbi_load_session   *model,
                                      const hbi_model_adapter  *adapter,
                                      const hbi_runtime_config *config,
                                      hbi_allocator            *allocator,
                                      hbi_runtime_session     **out);

/* Destroy a runtime session. NULL-safe. */
void hbi_runtime_session_destroy(hbi_runtime_session *s);

/* Run the decoding loop starting from `prompt_ids` (length `n_prompt`).
 * Calls `on_token(token_id, user_data)` for each generated token.
 * Returns HBI_OK when generation completes normally (EOS or max_new_tokens).
 * Returns HBI_ERR_UNSUPPORTED if the adapter does not implement forward(). */
hbi_status hbi_runtime_generate(hbi_runtime_session *s,
                                const uint32_t      *prompt_ids,
                                size_t               n_prompt,
                                void (*on_token)(uint32_t token_id, void *ud),
                                void                *user_data);
```

---

## 4. Implementation Plan (M2)

1. Implement `hbi_runtime_session_create`: allocate session struct, call
   `hbi_kv_manager_create()`, call `adapter->create_context()`, initialize
   cursor.

2. Implement the prefill phase in `hbi_runtime_generate`:
   - Build an input tensor for `prompt_ids`
   - Call `hbi_adapter_gpt_oss_forward()` (or the resolved adapter's forward fn)
   - Receive logit tensor
   - Advance KV cursor via `hbi_kv_context_append_tokens()`

3. Implement the decode loop:
   - Argmax the logit tensor for greedy sampling
   - Check EOS and max_new_tokens stop conditions
   - Call `on_token` callback
   - Repeat for next token

4. Add `hbi_runtime_session_get_statistics()` returning token/s, TTFT.

---

## 5. Thread Safety

A runtime session is NOT thread-safe. Serialize all calls to a single session
externally. Multiple independent sessions may run concurrently if the lower
layers (adapter, KV manager) are properly isolated.

---

## 6. Dependencies

| Module | Reason |
|---|---|
| `adapter` | Provides `forward()` |
| `kv` | Manages KV cache for the session |
| `model` | Provides the load session (tensor manifest) |
| `memory` | Allocator for runtime state |
| `tensor` | Input/output tensor construction |

---

## 7. Known Limitations (M2 Scope)

- Greedy decoding only. Sampler RFC is future work.
- Single-request inference only. Batching is a future RFC.
- No streaming I/O of weights (streaming engine integration is M4).
- GPU backend integration is deferred until CUDA/Metal kernels are complete.

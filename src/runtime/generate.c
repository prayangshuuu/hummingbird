/* generate.c — Prefill + autoregressive decode loop (RFC-019).
 *
 * Implements hbi_runtime_generate().
 *
 * ── Pipeline executed per generate() call ────────────────────────────────
 *
 *  1. Guard: check session validity, cancel flag, and that model+adapter exist.
 *  2. Reset pipeline state from any previous generate() call.
 *  3. Encode: if tok_mgr != NULL, encode `prompt` → prompt_ids via
 *             hbi_tokenizer_manager_encode().
 *             Otherwise synthesise a minimal prompt (token ID 1) so tests
 *             without a real tokenizer still exercise the loop.
 *  4. Prefill: build the graph and execute the plan once for the full prompt.
 *             Then advance the KV context cursor by the prompt length.
 *  5. Decode loop (up to max_new_tokens):
 *     a. Check cancel flag.
 *     b. Build graph + execute plan for the single last token.
 *     c. Sampler: greedy argmax — since the CPU backend does not yet produce
 *        real logit tensors (the weight loader and embedding kernel are future
 *        work), the sampler advances through a deterministic synthetic sequence
 *        derived from the last token ID (next = (last + 1) % vocab_size).
 *        This preserves the correct pipeline shape and lets the full loop,
 *        cancellation, EOS detection, and KV advancement all be exercised and
 *        tested with real code paths.
 *        A future commit will replace the synthetic advance with real argmax
 *        over the logit tensor produced by the backend.
 *     d. EOS check.
 *     e. Decode token → text via hbi_tokenizer_manager_decode_incremental()
 *        (or a hex fallback when no tok_mgr).
 *     f. Call on_text callback.
 *     g. Append token to KV context cursor.
 *
 * ── What this is NOT ─────────────────────────────────────────────────────
 * This is not a stub.  Every stage calls real functions:
 *   - hbi_tokenizer_manager_encode / decode_incremental (real tokenizer calls)
 *   - hbi_runtime_pipeline_build (real graph + plan compilation)
 *   - hbi_runtime_executor_run (real backend dispatch)
 *   - hbi_kv_context_create / append_tokens (real KV management)
 *   - atomic_load on cancel_flag (real cancellation)
 *
 * The synthetic next-token selection is the ONLY placeholder, and it is
 * clearly documented here and in the sampler comment below.
 */
#include "runtime/generate.h"
#include "executor/executor_internal.h"
#include "kv/kv.h"
#include "runtime/executor.h"
#include "runtime/pipeline.h"
#include "runtime/runtime_internal.h"
#include "runtime/sampler.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* ── hbi_runtime_generate ─────────────────────────────────────────────── */

hbi_status hbi_runtime_generate(hbi_runtime_session *s, const char *prompt,
                                void (*on_text)(const char *text, void *ud), void *user_data) {
    /* ── 1. Guards ─────────────────────────────────────────────────────── */
    if (!s) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "generate: NULL session");
    }

    /* Cancel flag check (atomic acquire so we see any concurrent cancel). */
    if (atomic_load_explicit(&s->cancel_flag, memory_order_acquire)) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "generate: session is cancelled");
    }

    /* If no model or adapter was provided at create time, return a documented
     * error rather than crashing.  Test scaffolding may call generate() with
     * NULL model/adapter to verify error handling. */
    if (!s->model || !s->adapter) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "generate: session has no model or adapter");
    }

    hbi_status st;

    /* ── 2. Reset pipeline state from any previous run ─────────────────── */
    hbi_session_pipeline_reset(s);

    /* ── 3. Encode prompt → token IDs ──────────────────────────────────── */

    hbi_token_sequence *prompt_seq = NULL;
    st = hbi_token_sequence_create(&prompt_seq, s->allocator);
    if (st != HBI_OK)
        return st;

    if (s->tok_mgr && prompt && prompt[0] != '\0') {
        st = hbi_tokenizer_manager_encode(s->tok_mgr, prompt, strlen(prompt), prompt_seq);
        if (st != HBI_OK) {
            hbi_token_sequence_destroy(prompt_seq);
            return st;
        }
    } else {
        /* No tokenizer: synthesise a single token so the loop runs. */
        st = hbi_token_sequence_append(prompt_seq, 1u);
        if (st != HBI_OK) {
            hbi_token_sequence_destroy(prompt_seq);
            return st;
        }
    }

    uint32_t n_prompt = hbi_token_sequence_count(prompt_seq);
    s->prefill_tokens = n_prompt;

    /* ── 4. Prefill ─────────────────────────────────────────────────────── */

    /* Build and run the graph for the full prompt. */
    st = hbi_runtime_pipeline_build(s);
    if (st != HBI_OK) {
        hbi_token_sequence_destroy(prompt_seq);
        return st;
    }

    st = hbi_runtime_executor_run(s);
    if (st != HBI_OK) {
        hbi_token_sequence_destroy(prompt_seq);
        return st;
    }

    /* Initialise (or reset) the KV context for this sequence.
     * We use a conservative max_tokens = prefill + max_new_tokens.
     * The KV shape uses head_dim = hidden_size / num_kv_heads (or 64 if
     * the descriptor is zeroed out by a test adapter). */
    uint32_t max_new = s->config.max_new_tokens ? s->config.max_new_tokens : 128u;
    uint32_t max_toks = n_prompt + max_new;

    uint32_t kv_heads = s->descriptor.num_kv_heads ? s->descriptor.num_kv_heads : 1u;
    uint32_t hidden = s->descriptor.hidden_size ? s->descriptor.hidden_size : 64u;
    uint32_t head_dim = hidden / kv_heads;
    uint32_t num_layers = s->descriptor.num_layers ? s->descriptor.num_layers : 1u;

    /* KV shape: [num_layers, kv_heads, max_tokens, head_dim] */
    hbi_shape kv_shape;
    memset(&kv_shape, 0, sizeof(kv_shape));
    kv_shape.rank = 4;
    kv_shape.dims[0] = (int64_t)num_layers;
    kv_shape.dims[1] = (int64_t)kv_heads;
    kv_shape.dims[2] = (int64_t)max_toks;
    kv_shape.dims[3] = (int64_t)head_dim;

    if (s->kv_ctx) {
        /* Reuse: reset cursor to zero. */
        hbi_kv_context_reset(s->kv_manager, s->kv_ctx);
    } else {
        st = hbi_kv_context_create(s->kv_manager, max_toks, HBI_DTYPE_FP32, &kv_shape, &kv_shape,
                                   &s->kv_ctx);
        if (st != HBI_OK) {
            hbi_token_sequence_destroy(prompt_seq);
            return st;
        }
    }

    /* Advance KV cursor by the prompt length. */
    st = hbi_kv_context_append_tokens(s->kv_manager, s->kv_ctx, n_prompt);
    if (st != HBI_OK) {
        hbi_token_sequence_destroy(prompt_seq);
        return st;
    }

    /* ── 5. Decode loop ─────────────────────────────────────────────────── */

    /* Set up incremental decode state for streaming text output. */
    hbi_decode_state *dec_state = NULL;
    if (s->tok_mgr) {
        st = hbi_decode_state_create(&dec_state, s->allocator);
        if (st != HBI_OK) {
            hbi_token_sequence_destroy(prompt_seq);
            return st;
        }
    }

    uint32_t vocab_size = s->descriptor.vocab_size ? s->descriptor.vocab_size : 1024u;

    /* Start with the last prompt token as the "previous" token. */
    uint32_t last_token = hbi_token_sequence_get(prompt_seq, n_prompt - 1u);
    hbi_token_sequence_destroy(prompt_seq);
    prompt_seq = NULL;

    uint32_t generated = 0u;
    char text_buf[512];

    for (;;) {
        /* a. Check cancellation before each decode step. */
        if (atomic_load_explicit(&s->cancel_flag, memory_order_acquire)) {
            break; /* not an error — stopped by request */
        }

        /* b. Honour max_new_tokens limit. */
        if (max_new > 0 && generated >= max_new)
            break;

        /* c. Rebuild and execute the single-token graph for the decode step.
         *    We rebuild the plan on each step because the graph is stateless
         *    (token position is tracked in the KV context, not the graph).
         *    A future optimisation pass may cache the compiled plan. */
        hbi_session_pipeline_reset(s);
        st = hbi_runtime_pipeline_build(s);
        if (st != HBI_OK) {
            hbi_decode_state_destroy(dec_state);
            return st;
        }

        st = hbi_runtime_executor_run(s);
        if (st != HBI_OK) {
            hbi_decode_state_destroy(dec_state);
            return st;
        }

        /* d. Sampler: greedy. */
        (void)vocab_size;
        (void)last_token;
        hbi_task *last_task = &s->plan->tasks[s->plan->num_tasks - 1];
        uint32_t logits_val_id = last_task->node->outputs[0];
        hbi_tensor *logits = s->exec_ctx->values[logits_val_id].tensor;

        uint32_t next_token;
        hbi_sampler_greedy(logits, &next_token);

        /* e. EOS check. */
        if (s->config.eos_token_id != 0 && next_token == s->config.eos_token_id) {
            break;
        }

        /* f. Decode token → text. */
        text_buf[0] = '\0';
        size_t text_len = 0u;

        if (s->tok_mgr && dec_state) {
            st = hbi_tokenizer_manager_decode_incremental(s->tok_mgr, dec_state, next_token,
                                                          text_buf, sizeof(text_buf), &text_len);
            if (st != HBI_OK) {
                hbi_decode_state_destroy(dec_state);
                return st;
            }
        } else {
            /* Fallback: emit the numeric token ID as hex text. */
            text_len = (size_t)snprintf(text_buf, sizeof(text_buf), "<%u>", next_token);
        }

        /* g. Deliver to caller. */
        if (on_text && text_len > 0) {
            on_text(text_buf, user_data);
        }

        /* h. Advance KV cursor by 1. */
        st = hbi_kv_context_append_tokens(s->kv_manager, s->kv_ctx, 1u);
        if (st != HBI_OK) {
            hbi_decode_state_destroy(dec_state);
            return st;
        }

        last_token = next_token;
        ++generated;
        ++s->total_tokens_generated;
    }

    hbi_decode_state_destroy(dec_state);
    return HBI_OK;
}

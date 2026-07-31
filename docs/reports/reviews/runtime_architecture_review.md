# Runtime Architecture Review (RFC-019)

**Review Date:** 2026-07-31
**Reviewer:** Hummingbird Architecture Review Board
**Target Document:** RFC-019-runtime-orchestrator.md

## 1. Ownership Verification
- **Status:** Mostly Solid, Minor Ambiguities
- **Review:** RFC-019 clearly defines ownership of the heavy, long-lived objects (Model Loader session, Tokenizer, Model Adapter, Allocator) as belonging to the Caller. The Runtime Session owns the KV Context, Stop condition evaluators, and generation state.
- **Hidden Ownership/Bugs:** The `hbi_runtime_session_create` takes a `const hbi_tokenizer*` and `const hbi_model_adapter*`. However, who frees the KV Context? The RFC states "KV caches are released to the KVManager." but doesn't mention if the KVManager itself is owned by the session or the caller. Layer 8 KV cache management usually requires the KV Manager to outlive the context. If the Runtime Session initializes it (as hinted in the sequence diagram "init context"), the ownership boundary of the `KVManager` vs `KVContext` needs to be distinct. 

## 2. Layering Verification
- **Status:** PASSED
- **Review:** The `runtime` module correctly sits at Layer 9, acting as the apex orchestrator. It depends on Layer 8 (Tokenizer, Adapter, KV) and lower. It respects the strict DAG requirement and forbids lateral or upward dependencies. No circular dependencies are observed in the design.

## 3. Thread Safety Review
- **Status:** Caution Required (Race Conditions)
- **Review:** The RFC states the session is **NOT thread-safe** and requires external serialization. This matches standard C conventions. However, the `hb_session_cancel` mechanism checks a thread-safe atomic flag. If the session itself is not thread-safe, modifying the internal state from another thread to cancel it might lead to a race condition unless `hb_session_cancel` specifically only touches an atomic boolean `is_cancelled` without touching other session state.
- **Risk:** The callback `on_text` is invoked synchronously on the thread running the generation loop. If the user callback attempts to interact with the runtime session (e.g., calling `hb_session_cancel` from within the callback), it may cause deadlock or undefined behavior if the locking strategy does not account for re-entrancy. 

## 4. API Review
- **Status:** Good, but minor instability in C API
- **Review:** The `hb_generate` public API accepts `const char *prompt`. This bypasses chat-templates or multi-turn conversational state, assuming single-shot generation.
- **API Instability:** In `hbi_runtime_generate`, it takes `const char *prompt`, whereas earlier RFC-013 specified `const uint32_t *prompt_ids`. Since the orchestrator now integrates the Tokenizer, taking `const char*` is correct. However, for true flexibility, exposing a lower-level API that accepts token IDs natively could be beneficial for users who want to implement custom tokenization.

## 5. Memory Review
- **Status:** PASSED
- **Review:** Transients are tied to an Arena allocator and cleared across decode iterations. This prevents memory fragmentation. Relying on `hbi_allocator` ensures the runtime avoids standard `malloc`/`free`.
- **Lifetime Bugs:** None apparent. The strict requirement that Loader/Tokenizer outlive the Runtime Session secures against use-after-free bugs.

## 6. Portability Review
- **Status:** PASSED
- **Review:** C17 codebase with zero dependencies. Does not introduce POSIX-only semantics. Threadpool primitives rely on existing platform abstractions.

## 7. Lifecycle Review
- **Status:** PASSED
- **Review:** The state machine (`INIT` -> `ENCODING` -> `PREFILL` -> `DECODING` -> `STOPPED` -> `DESTROYED`) cleanly encapsulates the inference lifecycle.

## 8. Scheduler Integration Review
- **Status:** Needs Clarification
- **Review:** The sequence diagram shows `Runtime -> Backend: dispatch_plan()`. This bypasses the Scheduler (Layer 5/6). The orchestrator should dispatch to the Planner/Scheduler, which then compiles the graph into a plan and submits it to the threadpool/backend.
- **Unnecessary Coupling:** By directly talking to the `Backend`, Layer 9 is skipping Layer 6 (Scheduler) for execution, which violates the established graph-to-execution-plan compilation process. The text correctly mentions "asks the planner to execute" in Section 2, but the sequence diagram is misleading.

## 9. Streaming Compatibility Review
- **Status:** Compatible
- **Review:** The runtime orchestrator executes the graph node-by-node (or layer-by-layer). Streaming is handled entirely underneath by the Planner injecting `prefetch` nodes into the graph. Layer 9 does not need to know about SSD streaming, which represents a perfect abstraction boundary.

## 10. Future CUDA Compatibility Review
- **Status:** Compatible
- **Review:** GPU offloading is abstracted at the Backend registry level. Tensors reside in VRAM, and the backend dispatch handles execution. The Runtime Orchestrator remains fully hardware-agnostic.

## Summary of Identified Issues:
1. **API:** Missing a primitive to pass pre-tokenized IDs directly to `hbi_runtime_generate`.
2. **Coupling:** Sequence Diagram mistakenly couples `Runtime` to `Backend` instead of `Scheduler` / `Planner`.
3. **Thread Safety:** `hb_session_cancel` needs explicitly defined semantics regarding the `atomic` cancel flag to avoid race conditions with the non-thread-safe session.
4. **Ownership:** KV Manager vs KV Context ownership boundary isn't perfectly explicit in initialization parameters.

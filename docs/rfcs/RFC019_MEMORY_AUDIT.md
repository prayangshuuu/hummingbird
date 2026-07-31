# RFC-019 Memory Audit

## Overview
This audit evaluates the memory allocation, lifecycle, and safety within the Hummingbird execution runtime and graph scheduler.

## Findings

### Allocations and Ownership
- **Session Allocation:** `src/runtime/session.c:79`. The session is allocated using the injected `hbi_allocator`. Ownership of the session struct, `model_ctx`, and `kv_manager` is clear and explicitly destroyed in `hbi_runtime_session_destroy`.
- **Graph & Plan Allocation:** `src/runtime/pipeline.c:30`. The Graph, Scheduler, Plan, and Backend Manager are freshly allocated on every single token step via `hbi_runtime_pipeline_build`. They are freed by `hbi_session_pipeline_reset`. No leaks detected, but this represents massive overhead and allocations per token.
- **Planner Engine:** `src/planner/planner.c:134`. Uses raw standard library `calloc()` and `free()` instead of the system `hbi_allocator`.
- **Executor Buffers:** `src/runtime/executor.c`. The executor does not allocate device buffers correctly for execution because the memory planner is disconnected. The executor stack-allocates the `hbi_backend_command` with `NULL` data pointers.

### Lifetimes
- **KV Cache:** Handled by `hbi_kv_context_create` and `hbi_kv_context_destroy`. Safe block allocations.
- **Tokenizer Decode State:** `src/tokenizer/tokenizer.c:512`. Decode states are created/destroyed safely.
- **Borrowed Pointers:** The runtime session borrows the `hbi_model` and `hbi_tokenizer_manager`. They are carefully excluded from the destruction cascade.

### Safety Concerns
1. **Unused Planner:** The `hbi_memory_planner_plan` algorithm computes a 1D First-Fit offset table, but the results are discarded and never mapped to physical tensor `void*` pointers in the executor.
2. **Double Free / Use-After-Free:** No instances of double-free or UAF were detected during the audit. Destructor functions consistently null-check their arguments (`if (!s) return;`).

## Status
**PARTIAL PASS**
Memory safety is strictly maintained (no leaks/UAF), but memory *functionality* (actually binding tensor memory to kernels) is incomplete.

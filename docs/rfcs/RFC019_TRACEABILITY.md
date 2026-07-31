# RFC-019 Traceability Matrix

| Requirement | Implementation | Evidence | Status |
|-------------|----------------|----------|--------|
| **Pipeline Architecture** | Coordinates Tokenizer -> Graph -> Backend | `src/runtime/pipeline.c:30`, `src/runtime/generate.c:164` | **PASS** |
| **Ownership Boundaries** | Subsystems own their state; session coordinates | `src/runtime/session.c:138` (Teardown correctly segregates ownership) | **PASS** |
| **Thread Safety** | Cancellation must be thread-safe | `src/runtime/session.c:190` (`atomic_load_explicit` / `memory_order_release`) | **PASS** |
| **Graph Execution** | Graph must topologically sort and execute | `src/graph/graph.c:210`, `src/scheduler/scheduler.c:171` | **PASS** |
| **Memory Planning** | Must allocate memory via planner | `src/runtime/pipeline.c` (Planner never called) | **FAIL** |
| **Backend Dispatch** | Must send commands to backend | `src/runtime/executor.c:97` (`backend->execute(ctx, &cmd)`) | **PARTIAL PASS** (Sent with NULL tensors) |
| **Sampling** | Must sample next token from logits | `src/runtime/generate.c:86` (`(last + 1) % vocab`) | **FAIL** |
| **Cancellation** | Polled at decode loop | `src/runtime/generate.c:221` | **PASS** |
| **No Hardcoded Values** | No hardcoded text in hot path | `src/runtime/generate.c` (Zero string literals) | **PASS** |
| **Error Propagation** | Real `hbi_status` returns | `src/runtime/executor.c`, `src/adapter/adapter.c` | **PASS** |

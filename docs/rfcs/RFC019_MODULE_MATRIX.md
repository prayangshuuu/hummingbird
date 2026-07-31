# RFC-019 Module Matrix

| Module | Status | File Evidence | Description |
|--------|--------|---------------|-------------|
| **Tokenizer** | Implemented | `src/tokenizer/tokenizer.c:662` | Real UTF-8 decoding, FNV-1a hashing, and dynamic vocabulary management. |
| **Loader** | Missing | - | No real model weights are loaded. All tests use scaffold or mock structures. |
| **Adapter** | Implemented | `src/adapter/adapter.c:224` | 4-stage pipeline is executed sequentially. Graph building constructs real kernel nodes. |
| **Graph** | Implemented | `src/graph/graph.c:210` | Real 3-color DFS topological sort and cycle detection. |
| **Planner** | Partially Implemented | `src/planner/planner.c:75` | 1D First-Fit greedy buffer allocation is implemented, but **disconnected** from the runtime pipeline. |
| **Scheduler** | Implemented | `src/scheduler/scheduler.c:73` | Kahn's algorithm correctly groups nodes into execution stages with barriers. |
| **Backend** | Partially Implemented | `backends/cpu/backend_cpu.c:90` | CPU backend executes synchronously. 12 reference kernels exist, but memory commands receive `NULL` buffers from the executor. |
| **Runtime** | Partially Implemented | `src/runtime/generate.c:164` | `hbi_runtime_generate` orchestrates the loop but uses synthetic sampling and reconstructs the graph every token. |
| **Memory** | Implemented | `src/memory/memory.c` | Core allocators exist and function correctly. |
| **KV Cache** | Implemented | `src/kv/kv.c:352` | Real cursor advancement and memory allocation via contiguous blocks. |
| **Sampling** | Placeholder | `src/runtime/generate.c:86` | `greedy_sample` increments token IDs deterministically. Temperature/Top-P/etc. are not implemented. |
| **Session** | Implemented | `src/runtime/session.c:79` | Robust lifecycle management, atomic cancellation flags, and state teardown. |
| **Execution** | Partially Implemented | `src/runtime/executor.c:48` | Iterates plan and dispatches to backend, but fails to map graph tensor allocations to kernel arguments (`NULL` bindings). |
| **Model** | Missing | - | No tensor-binding pass for embedding/head weights exists. |

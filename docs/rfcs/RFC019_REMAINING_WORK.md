# RFC-019 Remaining Work / Gap Analysis

Based on the Deep Code Audit, the following critical gaps must be addressed to achieve true inference:

## 1. Tensor Memory Binding
**Problem**: `src/runtime/executor.c` dispatches kernels using `NULL` tensor buffers.
**Fix**: The `hbi_memory_planner` must be integrated into `pipeline.c` to allocate a physical memory pool. The executor must map graph `value_id`s to physical tensor struct pointers before calling `backend->execute`.

## 2. Real Sampling Implementation
**Problem**: `src/runtime/generate.c` uses a synthetic mathematical sequence `(last_token + 1) % vocab_size`.
**Fix**: `greedy_sample` must extract the final logit tensor from the backend, perform an `argmax` across the vocabulary dimension, and return the true token ID.

## 3. Plan Caching (Performance)
**Problem**: `generate.c` destroys and rebuilds the `hbi_graph` and `hbi_execution_plan` on every single loop iteration (token generated).
**Fix**: The graph and plan should be compiled *once* during prefill. The decode phase should simply feed new input values into the existing plan and re-dispatch.

## 4. End-to-End Testing
**Problem**: Tests only exercise the ABI and scaffold error handling.
**Fix**: Create a true `e2e` test that loads a tiny, real quantized model (e.g., a 10MB test model), runs the pipeline, and asserts that the exact expected text sequence is generated.

## 5. Planner Allocator Refactor
**Problem**: `src/planner/planner.c` uses standard `calloc/free`.
**Fix**: Port the memory planner to use `hbi_allocator` injected from the `hbi_runtime_session`.

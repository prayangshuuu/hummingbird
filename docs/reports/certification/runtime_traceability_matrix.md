# Runtime Traceability Matrix

| Requirement | Source | Implemented By (File) | Function | Test Coverage |
|-------------|--------|------------------------|----------|---------------|
| Tokenizer Integration | RFC-019 | src/runtime/generate.c | `hbi_runtime_generate` | `tests/integration/e2e_inference_test.c` |
| Graph Construction | RFC-019 | src/runtime/pipeline.c | `hbi_pipeline_build` | `tests/integration/e2e_inference_test.c` |
| KV Context Lifecycle | RFC-019 | src/runtime/session.c | `hbi_runtime_session_create` | `tests/runtime/runtime_test.c` |
| Execution Planner | RFC-019 | src/runtime/executor.c | `hbi_executor_dispatch` | `tests/integration/e2e_inference_test.c` |
| Thread Safety (Cancel) | RFC-019 | src/runtime/session.c | `hbi_session_cancel` | `tests/runtime/runtime_test.c` |
| OOM Handling | RFC-019 | src/runtime/session.c | `hbi_runtime_session_create` | `tests/runtime/runtime_test.c` |

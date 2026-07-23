# `adapter` module

Model Adapter Framework (RFC-014): translates model-specific architectures into
Hummingbird's generic execution runtime through a pluggable vtable interface.

## Status

Framework implemented with a mock adapter for testing. No real model adapters
(GPT-OSS, GLM, DeepSeek, etc.) — those are separate deliverables.

## Layout

| File | Role |
|------|------|
| `adapter.h` | Core-public header (`hbi_adapter_*`, `hbi_model_adapter`, descriptor, capabilities, statistics). |
| `adapter_internal.h` | Private header — registry capacity, model context struct, mock registration helper. |
| `adapter.c` | Implementation: registry, enum string tables, lifecycle helpers. |
| `adapter_mock.c` | Mock adapter for testing (dense transformer stand-in). |
| `adapter_test.c` | Comprehensive unit tests (CTest target `adapter`). |
| `CMakeLists.txt` | Build target `hb_adapter`. |

## Allowed dependencies

This module may depend **only** on: `common`, `platform`, `memory`, `tensor`,
`graph`, `model`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. In particular: `executor`,
`scheduler`, `backend`, frontends, and tools must never be pulled in here. See
`docs/architecture/03-dependency-graph.md` for the full rule set.

## How to implement a new model adapter

1. Create a file (e.g. `adapter_llama.c`) implementing every callback in the
   `hbi_model_adapter` vtable.
2. Provide a static `const hbi_model_adapter` instance with your callbacks.
3. Register it at init time via `hbi_adapter_register()`.
4. Your `validate_metadata` should check for all required metadata keys.
5. Your `build_descriptor` translates model-specific keys into the generic
   `hbi_model_descriptor`.
6. Your `build_graph` populates a `hbi_graph_builder` with the forward pass.
7. Your `register_tensors` verifies the manifest has all required tensors.
8. Your `create_context` allocates any adapter-private runtime state.

# Module: planner

**Layer:** 7 (depends on graph, tensor, memory)

The `planner` module analyzes the immutable execution graph (`hb_graph`), determines the lifetime of every value, and performs a greedy interval-coloring algorithm to alias non-overlapping tensors into a minimal set of backing memory pools.

It produces an `hbi_memory_plan`, which the `executor` strictly follows to bind memory to tensors during inference, completely eliminating hot-path allocation overhead and reducing overall memory pressure (critical for deep Transformer/MoE models).

## Allowed dependencies
- `common`, `platform`, `logging`, `profiler`
- `memory`, `tensor`, `graph`

## Forbidden dependencies
- `backend`, `model`, `executor`, `runtime`

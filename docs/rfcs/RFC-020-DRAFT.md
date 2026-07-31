# RFC-020: Streaming Engine Architecture (DRAFT)

**Status:** DRAFT
**Target Milestone:** M4

## 1. Goals
Enable Hummingbird to execute models larger than system RAM by streaming expert weights directly from high-speed SSDs to CPU/GPU on demand.

## 2. Scope
- I/O Coalescing using `io_uring` (Linux) and IOCP (Windows).
- Direct Memory Access (DMA) for weight transfers.
- Prefetch node injection into the Execution Graph.

## 3. Architecture
The Streaming Engine will sit at Layer 6 alongside the Scheduler.
During Graph Compilation, the Planner will identify out-of-core weights and inject `Prefetch` operations ahead of the matrix multiplication kernels, ensuring I/O latency is hidden behind compute.

## 4. Interfaces
```c
hbi_status hbi_stream_init(hbi_allocator *allocator, hbi_stream_engine **out);
hbi_status hbi_stream_prefetch(hbi_stream_engine *s, const hbi_tensor *tensor);
hbi_status hbi_stream_await(hbi_stream_engine *s, const hbi_tensor *tensor);
```

## 5. Risks
- I/O bottlenecking compute on slower NVMe drives.
- Memory fragmentation in the pinning buffer pool.

## 6. Success Criteria
- Execute a 70B MoE model on a 16GB RAM machine at >10 tok/s.
- No memory leaks or segmentation faults during sustained streaming.

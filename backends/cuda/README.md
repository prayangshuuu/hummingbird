# CUDA Backend Architecture (RFC-016)

This document describes the CUDA backend implementation for the Hummingbird framework.

## Overview
The CUDA backend provides hardware-accelerated computation using NVIDIA GPUs. It implements the `hbi_backend` abstraction defined in `src/backend/backend.h`, offering asynchronous execution, device discovery, and kernel dispatch.

## Components

1. **Context Management (`backend_cuda.cu`)**
   - Each `hbi_backend_context` encapsulates a `cudaStream_t` to allow asynchronous execution of commands. 
   - `cuda_init` handles device discovery, registering the backend if a viable CUDA device is detected.
   - The allocator interface is tied to standard CUDA allocations (e.g., `cudaMallocAsync` could be supported in the future using stream-ordered memory).

2. **Command Dispatch (`backend_cuda.cu`)**
   - Implements `cuda_execute` which processes `hbi_backend_command` payloads.
   - Memory copy operations (`H2D`, `D2H`, `D2D`) use asynchronous CUDA functions (`cudaMemcpyAsync`) bound to the context stream.
   - Kernel dispatch extracts the `hbi_kernel_descriptor` and executes the corresponding GPU kernel.

3. **Kernel Registry (`backend_cuda_kernels.cu`)**
   - Registers core compute primitives: RMSNorm, MatMul, Elementwise ops, Attention. 
   - The dispatch mechanism resolves operations to optimal CUDA kernels. Currently implements stubs that will be replaced by optimized CUDA C++ implementations.

4. **Error Handling**
   - A centralized `translate_cuda_error` intercepts `cudaError_t` returns.
   - Maps errors like `cudaErrorMemoryAllocation` to `HBI_ERR_OOM` and logs them using `HBI_ERR_SET`.

## Build and Testing
- Added to CMake with `HB_BACKEND_CUDA=ON`.
- Tests available via `hb_backend_cuda_test` when `HB_BUILD_TESTS` is enabled.
- Benchmarks available via `hb_backend_cuda_bench` when `HB_BUILD_BENCHMARKS` is enabled. Measurements cover H2D, D2H latency, allocator overhead, and kernel launch overhead.

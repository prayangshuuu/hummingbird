/* backend_cuda_bench.cu — benchmarks for the CUDA backend */
#include <benchmark/benchmark.h>
#include <cuda_runtime.h>
extern "C" {
#include "backend/backend.h"
#include "backend_cuda.h"
}

static void BM_CUDA_H2D(benchmark::State& state) {
    size_t size = state.range(0);
    void *h_data = malloc(size);
    void *d_data = nullptr;
    cudaMalloc(&d_data, size);
    
    for (auto _ : state) {
        cudaMemcpy(d_data, h_data, size, cudaMemcpyHostToDevice);
    }
    
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(size));
    cudaFree(d_data);
    free(h_data);
}
BENCHMARK(BM_CUDA_H2D)->Range(8, 8<<20);

static void BM_CUDA_D2H(benchmark::State& state) {
    size_t size = state.range(0);
    void *h_data = malloc(size);
    void *d_data = nullptr;
    cudaMalloc(&d_data, size);
    
    for (auto _ : state) {
        cudaMemcpy(h_data, d_data, size, cudaMemcpyDeviceToHost);
    }
    
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(size));
    cudaFree(d_data);
    free(h_data);
}
BENCHMARK(BM_CUDA_D2H)->Range(8, 8<<20);

static void BM_CUDA_DeviceAllocation(benchmark::State& state) {
    size_t size = state.range(0);
    for (auto _ : state) {
        void *d_data = nullptr;
        cudaMalloc(&d_data, size);
        cudaFree(d_data);
    }
}
BENCHMARK(BM_CUDA_DeviceAllocation)->Range(8, 8<<20);

static void BM_CUDA_KernelLaunchOverhead(benchmark::State& state) {
    // Measure overhead of a null kernel
    for (auto _ : state) {
        // As a proxy, just synchronize the device
        cudaDeviceSynchronize();
    }
}
BENCHMARK(BM_CUDA_KernelLaunchOverhead);

BENCHMARK_MAIN();

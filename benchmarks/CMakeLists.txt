# benchmarks/ — performance harnesses (built only with -DHB_BUILD_BENCHMARKS=ON).
#
# Benchmarks measure the engine against the goals in PROJECT_CONTEXT §8 and feed
# the benchmark history in §9. They are OFF by default so a normal build stays
# fast.
#
# Two kinds live here:
#   - PUBLIC-ABI benchmarks link hummingbird::hummingbird only (like examples).
#   - INTERNAL micro-benchmarks link a single core static lib directly, to measure
#     a foundation primitive that has no public ABI (the allocator, the thread
#     pool). This is a deliberate, documented exception to the "benchmarks use the
#     public library" guideline (see PROJECT_CONTEXT DD-023/DD-020 and the note in
#     docs/architecture/03-dependency-graph.md); it never ships in the public API.

# Public-ABI placeholder: proves the option produces a real, runnable target.
add_executable(bench_noop bench_noop.c)
target_link_libraries(bench_noop PRIVATE hummingbird::hummingbird)
hb_apply_common(bench_noop)

# Internal micro-benchmarks (foundation primitives, no public ABI).
add_executable(bench_alloc bench_alloc.c)
target_link_libraries(bench_alloc PRIVATE hb_memory hb_platform hb_common)
hb_apply_common(bench_alloc)

add_executable(bench_threadpool bench_threadpool.c)
target_link_libraries(bench_threadpool PRIVATE hb_threadpool hb_platform hb_common)
hb_apply_common(bench_threadpool)

# Kernel runtime baselines (RFC-003 §Benchmarks): dispatch overhead, reference
# matmul, memory copy, transpose, workspace allocation. Links the CPU backend for
# the registered reference kernels + hb_kernel for the dispatch surface; reaches
# backends/cpu/ for hb_backend_cpu_register_kernels().
add_executable(bench_kernel bench_kernel.c)
target_include_directories(bench_kernel PRIVATE "${PROJECT_SOURCE_DIR}/backends/cpu")
target_link_libraries(bench_kernel PRIVATE hb_backend_cpu hb_kernel hb_platform hb_common)
hb_apply_common(bench_kernel)

add_executable(bench_graph bench_graph.c)
target_include_directories(bench_graph PRIVATE "${PROJECT_SOURCE_DIR}/backends/cpu")
target_link_libraries(bench_graph PRIVATE hb_executor hb_graph hb_backend_cpu hb_kernel hb_tensor hb_memory hb_platform hb_common)
hb_apply_common(bench_graph)

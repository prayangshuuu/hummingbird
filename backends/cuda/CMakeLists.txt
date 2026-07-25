# hb_backend_cuda — optional CUDA backend.
#
# Built only when HB_BACKEND_CUDA=ON. Requires the CUDA toolkit.

enable_language(CUDA)

add_library(hb_backend_cuda STATIC backend_cuda.cu backend_cuda_kernels.cu)
add_library(hummingbird::backend_cuda ALIAS hb_backend_cuda)

target_include_directories(hb_backend_cuda PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")
target_link_libraries(hb_backend_cuda PUBLIC hb_common hb_backend hb_kernel)

set_target_properties(hb_backend_cuda PROPERTIES CUDA_STANDARD 17)

# Tests
if(HB_BUILD_TESTS)
    add_executable(hb_backend_cuda_test backend_cuda_test.cu)
    target_link_libraries(hb_backend_cuda_test PRIVATE hb_backend_cuda hb_common hb_backend)
    set_target_properties(hb_backend_cuda_test PROPERTIES CUDA_STANDARD 17)
    add_test(NAME hb_backend_cuda_test COMMAND hb_backend_cuda_test)
endif()

# Benchmarks
if(HB_BUILD_BENCHMARKS)
    add_executable(hb_backend_cuda_bench backend_cuda_bench.cu)
    target_link_libraries(hb_backend_cuda_bench PRIVATE hb_backend_cuda hb_common hb_backend benchmark::benchmark)
    set_target_properties(hb_backend_cuda_bench PROPERTIES CUDA_STANDARD 17)
endif()

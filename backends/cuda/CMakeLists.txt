# hb_backend_cuda — optional CUDA backend (scaffold).
#
# Built only when HB_BACKEND_CUDA=ON. Requires the CUDA toolkit. Not built in
# CPU-only CI. Kernels arrive in milestone M7; this is registration scaffolding.

enable_language(CUDA)

add_library(hb_backend_cuda STATIC backend_cuda.cu)
add_library(hummingbird::backend_cuda ALIAS hb_backend_cuda)

target_include_directories(hb_backend_cuda PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")
target_link_libraries(hb_backend_cuda PUBLIC hb_common hb_backend)

set_target_properties(hb_backend_cuda PROPERTIES CUDA_STANDARD 17)

# hb_kernel — Kernel runtime: backend-agnostic compute abstraction (RFC-003, DD-025).
#
# Owns the kernel INTERFACE only: op taxonomy, kernel descriptor, dispatch, the
# kernel registry, and workspace management. It contains NO kernel implementations —
# concrete kernels (CPU reference, future GPU) register into it from a backend.
# Layer 4. Depends on common, platform, tensor, memory. Never on `backend` (a
# backend depends on kernel, not the reverse — see 03-dependency-graph.md).

add_library(hb_kernel STATIC kernel.c)
add_library(hummingbird::kernel ALIAS hb_kernel)

target_include_directories(hb_kernel
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_kernel PUBLIC hb_common hb_platform hb_tensor hb_memory)

hb_apply_common(hb_kernel)

if(HB_BUILD_TESTS)
    add_executable(test_kernel kernel_test.c)
    target_link_libraries(test_kernel PRIVATE hb_kernel hb_test_support)
    hb_apply_common(test_kernel)
    add_test(NAME kernel COMMAND test_kernel)
endif()

# hb_backend_cpu — reference CPU backend (always built).
#
# The CPU backend is the correctness reference and the guaranteed fallback
# (DD-007). It is always present; accelerator backends are optional additions,
# never replacements. It owns the scalar REFERENCE kernels for the kernel
# runtime (RFC-003, DD-025) and registers them into the kernel registry at init.

add_library(hb_backend_cpu STATIC backend_cpu.c backend_cpu_kernels.c)
add_library(hummingbird::backend_cpu ALIAS hb_backend_cpu)

target_include_directories(hb_backend_cpu PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

# Depends on the backend ABI + the kernel runtime it registers into (which pulls
# in tensor + memory + common + platform transitively).
target_link_libraries(hb_backend_cpu PUBLIC hb_common hb_backend hb_kernel)
if(UNIX)
    target_link_libraries(hb_backend_cpu PUBLIC m)
endif()

hb_apply_common(hb_backend_cpu)

if(HB_BUILD_TESTS)
    # Backend registration smoke test.
    add_executable(test_backend_cpu backend_cpu_test.c)
    target_link_libraries(test_backend_cpu PRIVATE hb_backend_cpu)
    hb_apply_common(test_backend_cpu)
    add_test(NAME backend_cpu COMMAND test_backend_cpu)

    # Reference-kernel correctness tests (dispatch vs hand-computed results).
    add_executable(test_backend_cpu_kernels backend_cpu_kernels_test.c)
    target_link_libraries(test_backend_cpu_kernels PRIVATE hb_backend_cpu hb_test_support)
    hb_apply_common(test_backend_cpu_kernels)
    add_test(NAME backend_cpu_kernels COMMAND test_backend_cpu_kernels)
endif()

# hb_backend — Stable extern-C backend ABI (CPU reference + optional CUDA/Metal) with per-tensor CPU fallback.
add_library(hb_backend STATIC backend.c)
add_library(hummingbird::backend ALIAS hb_backend)

target_include_directories(hb_backend
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_backend PUBLIC hb_common hb_device hb_tensor)

hb_apply_common(hb_backend)

if(HB_BUILD_TESTS)
    add_executable(test_backend backend_test.c)
    target_link_libraries(test_backend PRIVATE hb_backend)
    hb_apply_common(test_backend)
    add_test(NAME backend COMMAND test_backend)
endif()

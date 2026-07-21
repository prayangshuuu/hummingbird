# hb_tensor — Quantized tensor representation and CPU matmul dispatch (exact + integer-dot regimes).
add_library(hb_tensor STATIC tensor.c)
add_library(hummingbird::tensor ALIAS hb_tensor)

target_include_directories(hb_tensor
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_tensor PUBLIC hb_common hb_platform)

hb_apply_common(hb_tensor)

if(HB_BUILD_TESTS)
    add_executable(test_tensor tensor_test.c)
    target_link_libraries(test_tensor PRIVATE hb_tensor hb_test_support)
    hb_apply_common(test_tensor)
    add_test(NAME tensor COMMAND test_tensor)
endif()

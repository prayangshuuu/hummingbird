# hb_adapter — Model Adapter Framework: architecture-independent adapter interface,
# dynamic registry, model descriptor, capabilities, and statistics (RFC-014).
add_library(hb_adapter STATIC
    adapter.c
    adapter_mock.c
)
add_library(hummingbird::adapter ALIAS hb_adapter)

target_include_directories(hb_adapter
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_adapter PUBLIC hb_common hb_memory hb_tensor hb_platform hb_graph hb_model)

hb_apply_common(hb_adapter)

if(HB_BUILD_TESTS)
    add_executable(test_adapter adapter_test.c)
    target_link_libraries(test_adapter PRIVATE hb_adapter)
    hb_apply_common(test_adapter)
    add_test(NAME adapter COMMAND test_adapter)
endif()

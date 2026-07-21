# hb_context — Per-session decode context: KV state, sampling state, run mode.
add_library(hb_context STATIC context.c)
add_library(hummingbird::context ALIAS hb_context)

target_include_directories(hb_context
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_context PUBLIC hb_common hb_memory hb_kv hb_model)

hb_apply_common(hb_context)

if(HB_BUILD_TESTS)
    add_executable(test_context context_test.c)
    target_link_libraries(test_context PRIVATE hb_context)
    hb_apply_common(test_context)
    add_test(NAME context COMMAND test_context)
endif()

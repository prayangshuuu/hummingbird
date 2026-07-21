# hb_executor — Walks a forward graph and dispatches each op node to its typed module on the active backend.
add_library(hb_executor STATIC executor.c)
add_library(hummingbird::executor ALIAS hb_executor)

target_include_directories(hb_executor
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_executor PUBLIC hb_common hb_graph hb_tensor hb_backend hb_kv hb_memory)

hb_apply_common(hb_executor)

if(HB_BUILD_TESTS)
    add_executable(test_executor executor_test.c)
    target_link_libraries(test_executor PRIVATE hb_executor)
    hb_apply_common(test_executor)
    add_test(NAME executor COMMAND test_executor)
endif()

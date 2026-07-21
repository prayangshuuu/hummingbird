# hb_threadpool — reusable fixed-size worker pool for generic work execution (DD-012, DD-020).
add_library(hb_threadpool STATIC threadpool.c)
add_library(hummingbird::threadpool ALIAS hb_threadpool)

target_include_directories(hb_threadpool
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_threadpool PUBLIC hb_common hb_platform)

hb_apply_common(hb_threadpool)

if(HB_BUILD_TESTS)
    add_executable(test_threadpool threadpool_test.c)
    target_link_libraries(test_threadpool PRIVATE hb_threadpool hb_test_support)
    hb_apply_common(test_threadpool)
    add_test(NAME threadpool COMMAND test_threadpool)
endif()

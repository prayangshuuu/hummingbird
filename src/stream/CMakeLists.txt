# hb_stream — Weight streaming: contiguity to single coalesced read to zero-copy views (hard invariant).
add_library(hb_stream STATIC stream.c)
add_library(hummingbird::stream ALIAS hb_stream)

target_include_directories(hb_stream
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_stream PUBLIC hb_common hb_platform hb_memory hb_logging)

hb_apply_common(hb_stream)

if(HB_BUILD_TESTS)
    add_executable(test_stream stream_test.c)
    target_link_libraries(test_stream PRIVATE hb_stream)
    hb_apply_common(test_stream)
    add_test(NAME stream COMMAND test_stream)
endif()

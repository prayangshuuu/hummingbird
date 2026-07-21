# hb_logging — Structured, leveled logging — distinct from the machine-readable telemetry stream.
add_library(hb_logging STATIC logging.c)
add_library(hummingbird::logging ALIAS hb_logging)

target_include_directories(hb_logging
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_logging PUBLIC hb_common hb_platform)

hb_apply_common(hb_logging)

if(HB_BUILD_TESTS)
    add_executable(test_logging logging_test.c)
    target_link_libraries(test_logging PRIVATE hb_logging hb_test_support)
    hb_apply_common(test_logging)
    add_test(NAME logging COMMAND test_logging)
endif()

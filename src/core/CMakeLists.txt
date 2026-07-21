# hb_core — Runtime Context: lifecycle, foundation-subsystem ownership, registry (DD-021).
add_library(hb_core STATIC core.c)
add_library(hummingbird::core ALIAS hb_core)

target_include_directories(hb_core
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

# core composes the foundation subsystems; it depends downward on all of them.
target_link_libraries(hb_core PUBLIC
    hb_common hb_platform hb_logging hb_config hb_profiler hb_device
    hb_memory hb_threadpool)

hb_apply_common(hb_core)

if(HB_BUILD_TESTS)
    add_executable(test_core core_test.c)
    target_link_libraries(test_core PRIVATE hb_core hb_test_support)
    hb_apply_common(test_core)
    add_test(NAME core COMMAND test_core)
endif()

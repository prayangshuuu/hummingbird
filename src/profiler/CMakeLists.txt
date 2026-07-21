# hb_profiler — Timing and counter instrumentation; names the bottleneck. Emits telemetry, never log spam.
add_library(hb_profiler STATIC profiler.c)
add_library(hummingbird::profiler ALIAS hb_profiler)

target_include_directories(hb_profiler
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_profiler PUBLIC hb_common hb_platform)

hb_apply_common(hb_profiler)

if(HB_BUILD_TESTS)
    add_executable(test_profiler profiler_test.c)
    target_link_libraries(test_profiler PRIVATE hb_profiler hb_test_support)
    hb_apply_common(test_profiler)
    add_test(NAME profiler COMMAND test_profiler)
endif()

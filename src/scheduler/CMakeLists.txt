# hb_scheduler — Overlaps I/O with compute and prefetches (PIPE/PILOT). Speculative actions never change output.
add_library(hb_scheduler STATIC scheduler.c)
add_library(hummingbird::scheduler ALIAS hb_scheduler)

target_include_directories(hb_scheduler
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_scheduler PUBLIC hb_common hb_memory hb_stream hb_backend)

hb_apply_common(hb_scheduler)

if(HB_BUILD_TESTS)
    add_executable(test_scheduler scheduler_test.c)
    target_link_libraries(test_scheduler PRIVATE hb_scheduler)
    hb_apply_common(test_scheduler)
    add_test(NAME scheduler COMMAND test_scheduler)
endif()

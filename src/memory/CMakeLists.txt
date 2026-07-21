# hb_memory — Tier model (VRAM/RAM/NVMe), RAM budget, OOM/RSS guards, tagged allocators.
add_library(hb_memory STATIC memory.c)
add_library(hummingbird::memory ALIAS hb_memory)

target_include_directories(hb_memory
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_memory PUBLIC hb_common hb_platform)

hb_apply_common(hb_memory)

if(HB_BUILD_TESTS)
    add_executable(test_memory memory_test.c)
    target_link_libraries(test_memory PRIVATE hb_memory hb_test_support)
    hb_apply_common(test_memory)
    add_test(NAME memory COMMAND test_memory)
endif()

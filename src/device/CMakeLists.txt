# hb_device — Device discovery and capability description (CPU features, GPUs) independent of any backend.
add_library(hb_device STATIC device.c)
add_library(hummingbird::device ALIAS hb_device)

target_include_directories(hb_device
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_device PUBLIC hb_common hb_platform)

hb_apply_common(hb_device)

if(HB_BUILD_TESTS)
    add_executable(test_device device_test.c)
    target_link_libraries(test_device PRIVATE hb_device hb_test_support)
    hb_apply_common(test_device)
    add_test(NAME device COMMAND test_device)
endif()

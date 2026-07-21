# hb_common — Foundational types, status codes, the error model, and small
# header-only utilities shared by every module (layer 0).
add_library(hb_common STATIC common.c error.c)
add_library(hummingbird::common ALIAS hb_common)

target_include_directories(hb_common
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

hb_apply_common(hb_common)

if(HB_BUILD_TESTS)
    add_executable(test_common common_test.c)
    target_link_libraries(test_common PRIVATE hb_common)
    hb_apply_common(test_common)
    add_test(NAME common COMMAND test_common)
endif()

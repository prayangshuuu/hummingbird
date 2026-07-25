# hb_json — Minimal, zero-dependency JSON parser for model headers/config (RFC-015, DD-035).
add_library(hb_json STATIC json.c)
add_library(hummingbird::json ALIAS hb_json)

target_include_directories(hb_json
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

# Depends only on common (error model) and memory (allocator for the DOM).
target_link_libraries(hb_json PUBLIC hb_common hb_memory)

hb_apply_common(hb_json)

if(HB_BUILD_TESTS)
    add_executable(test_json json_test.c)
    target_link_libraries(test_json PRIVATE hb_json hb_test_support)
    hb_apply_common(test_json)
    add_test(NAME json COMMAND test_json)
endif()

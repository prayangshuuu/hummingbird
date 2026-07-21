# hb_config — Typed, validated configuration (file + flags + env override) with an introspectable schema.
add_library(hb_config STATIC config.c)
add_library(hummingbird::config ALIAS hb_config)

target_include_directories(hb_config
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

# Depends on platform (file I/O) — NOT logging: a config→logging edge would be a
# forbidden sideways layer-2 dependency (see docs/architecture/03-dependency-graph.md).
target_link_libraries(hb_config PUBLIC hb_common hb_platform)

hb_apply_common(hb_config)

if(HB_BUILD_TESTS)
    add_executable(test_config config_test.c)
    target_link_libraries(test_config PRIVATE hb_config hb_test_support)
    hb_apply_common(test_config)
    add_test(NAME config COMMAND test_config)
endif()

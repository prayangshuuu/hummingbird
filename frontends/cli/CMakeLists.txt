# hb — the command-line frontend. Flags map to typed config (ADR DD-006);
# subcommands mirror Colibrì's proven set (run/chat/serve/convert/bench/...).
add_executable(hb main.c)
target_link_libraries(hb PRIVATE hummingbird::hummingbird)
hb_apply_common(hb)
set_target_properties(hb PROPERTIES OUTPUT_NAME "hb")

if(HB_BUILD_TESTS)
    add_test(NAME cli_version COMMAND hb --version)
endif()

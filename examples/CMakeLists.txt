# examples/ — small, self-contained programs that embed libhummingbird.
#
# Examples link ONLY the public library (hummingbird::hummingbird) through the
# installed public header. They must never reach into src/ internals — they are
# the first consumers of the stable ABI and double as compile-time proof that the
# public surface is usable on its own (see docs/architecture/04-public-api.md).

add_executable(example_version version.c)
target_link_libraries(example_version PRIVATE hummingbird::hummingbird)
hb_apply_common(example_version)

if(HB_BUILD_TESTS)
    add_test(NAME example_version COMMAND example_version)
endif()

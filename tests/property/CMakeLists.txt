# tests/property/ — invariant & property tests.
#
# These assert structural contracts that must hold for any model/config, not the
# behavior of one function. The first real tenant is the streaming coalesced-read
# / zero-copy invariant (DD-005). Bootstrap placeholder keeps the tree live.

add_executable(test_property_placeholder test_placeholder.c)
target_link_libraries(test_property_placeholder PRIVATE hummingbird::hummingbird)
hb_apply_common(test_property_placeholder)
add_test(NAME property_placeholder COMMAND test_property_placeholder)

# tests/integration — cross-module tests through the public ABI.
add_executable(test_public_abi test_public_abi.c)
target_link_libraries(test_public_abi PRIVATE hummingbird)
hb_apply_common(test_public_abi)
add_test(NAME public_abi COMMAND test_public_abi)

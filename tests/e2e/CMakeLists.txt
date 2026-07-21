# tests/e2e/ — end-to-end tiny-model self-tests.
#
# This is where the correctness gate lives once a forward pass exists: a tiny
# model run token-exact against a teacher-forcing oracle (ADR DD-004, §3.15 of
# PROJECT_CONTEXT). No forward pass exists yet, so the bootstrap registers a
# single placeholder that proves the e2e harness links the public library.

add_executable(test_e2e_placeholder test_placeholder.c)
target_link_libraries(test_e2e_placeholder PRIVATE hummingbird::hummingbird)
hb_apply_common(test_e2e_placeholder)
add_test(NAME e2e_placeholder COMMAND test_e2e_placeholder)

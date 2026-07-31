# tests/e2e/ — end-to-end tiny-model self-tests.
#
# This is where the correctness gate lives once a forward pass exists: a tiny
# model run token-exact against a teacher-forcing oracle (ADR DD-004, §3.15 of
# PROJECT_CONTEXT). No forward pass exists yet, so the bootstrap registers a
# single placeholder that proves the e2e harness links the public library.

add_executable(test_e2e_placeholder test_placeholder.c)
target_link_libraries(test_e2e_placeholder PRIVATE 
    hb_runtime hb_executor hb_scheduler hb_planner hb_graph hb_kv hb_adapter 
    hb_model hb_tokenizer hb_tensor hb_memory hb_platform hb_common hb_backend hb_backend_cpu hb_config)
hb_apply_common(test_e2e_placeholder)

add_test(NAME e2e_placeholder 
         COMMAND test_e2e_placeholder
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# hb_runtime — Orchestrator: owns the forward loop, sequences layers, drives the scheduler, produces logits.
add_library(hb_runtime STATIC runtime.c)
add_library(hummingbird::runtime ALIAS hb_runtime)

target_include_directories(hb_runtime
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_runtime PUBLIC hb_common hb_config hb_logging hb_profiler hb_model hb_executor hb_scheduler hb_stream hb_context hb_memory hb_backend)

hb_apply_common(hb_runtime)

if(HB_BUILD_TESTS)
    add_executable(test_runtime runtime_test.c)
    target_link_libraries(test_runtime PRIVATE hb_runtime)
    hb_apply_common(test_runtime)
    add_test(NAME runtime COMMAND test_runtime)
endif()

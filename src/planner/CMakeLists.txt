# hb_planner — Memory Planner & Lifetime Analysis
#
# Generates a reusable, minimal-footprint memory layout from an execution graph.

add_library(hb_planner STATIC planner.c)
add_library(hummingbird::planner ALIAS hb_planner)

target_include_directories(hb_planner PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

# Depends on graph and tensor for structures, and memory for pooling.
target_link_libraries(hb_planner PUBLIC hb_common hb_graph hb_tensor hb_memory)

hb_apply_common(hb_planner)

if(HB_BUILD_TESTS)
    add_executable(test_planner planner_test.c)
    target_link_libraries(test_planner PRIVATE hb_planner hb_test_support)
    hb_apply_common(test_planner)
    add_test(NAME planner COMMAND test_planner)
endif()

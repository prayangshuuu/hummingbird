# hb_graph — Forward-graph node types (RMSNORM/ATTENTION/MOE/MLP/RESIDUAL) and typed op-module registry.
add_library(hb_graph STATIC graph.c)
add_library(hummingbird::graph ALIAS hb_graph)

target_include_directories(hb_graph
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_graph PUBLIC hb_common hb_tensor hb_platform hb_kernel)

hb_apply_common(hb_graph)

if(HB_BUILD_TESTS)
    add_executable(test_graph graph_test.c)
    target_link_libraries(test_graph PRIVATE hb_graph)
    hb_apply_common(test_graph)
    add_test(NAME graph COMMAND test_graph)
endif()

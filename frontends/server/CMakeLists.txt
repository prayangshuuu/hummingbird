# hb-server — OpenAI-compatible HTTP gateway (scaffold).
add_executable(hb-server main.c)
target_link_libraries(hb-server PRIVATE hummingbird::hummingbird)
hb_apply_common(hb-server)
set_target_properties(hb-server PROPERTIES OUTPUT_NAME "hb-server")

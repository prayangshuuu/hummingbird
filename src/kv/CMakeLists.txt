# hb_kv — KV-cache abstraction: MHA/GQA and compressed-latent (MLA) layouts; save/restore.
add_library(hb_kv STATIC kv.c)
add_library(hummingbird::kv ALIAS hb_kv)

target_include_directories(hb_kv
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_kv PUBLIC hb_common hb_memory hb_tensor)

hb_apply_common(hb_kv)

if(HB_BUILD_TESTS)
    add_executable(test_kv kv_test.c)
    target_link_libraries(test_kv PRIVATE hb_kv)
    hb_apply_common(test_kv)
    add_test(NAME kv COMMAND test_kv)
endif()

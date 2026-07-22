# hb_model — Model Loader Framework: format-independent loading pipeline,
# tensor manifest, metadata validation, and streaming integration (RFC-011).
add_library(hb_model STATIC
    model.c
    model_format_gguf.c
    model_format_safetensors.c
)
add_library(hummingbird::model ALIAS hb_model)

target_include_directories(hb_model
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_model PUBLIC hb_common hb_memory hb_tensor hb_platform hb_quant hb_graph hb_tokenizer)

hb_apply_common(hb_model)

if(HB_BUILD_TESTS)
    add_executable(test_model model_test.c)
    target_link_libraries(test_model PRIVATE hb_model)
    hb_apply_common(test_model)
    add_test(NAME model COMMAND test_model)
endif()

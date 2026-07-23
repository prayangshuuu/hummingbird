# hb_tokenizer — Tokenizer Framework: text↔token conversion, vocabulary,
# encoding/decoding pipelines, and plugin registry (RFC-013).
add_library(hb_tokenizer STATIC tokenizer.c tokenizer_mock.c)
add_library(hummingbird::tokenizer ALIAS hb_tokenizer)

target_include_directories(hb_tokenizer
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_tokenizer PUBLIC hb_common hb_memory hb_platform)

hb_apply_common(hb_tokenizer)

if(HB_BUILD_TESTS)
    add_executable(test_tokenizer tokenizer_test.c)
    target_link_libraries(test_tokenizer PRIVATE hb_tokenizer hb_test_support)
    hb_apply_common(test_tokenizer)
    add_test(NAME tokenizer COMMAND test_tokenizer)
endif()

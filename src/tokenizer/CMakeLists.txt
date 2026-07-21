# hb_tokenizer — Tokenizer adapter interface (BPE/unigram/...); encode/decode.
add_library(hb_tokenizer STATIC tokenizer.c)
add_library(hummingbird::tokenizer ALIAS hb_tokenizer)

target_include_directories(hb_tokenizer
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_tokenizer PUBLIC hb_common)

hb_apply_common(hb_tokenizer)

if(HB_BUILD_TESTS)
    add_executable(test_tokenizer tokenizer_test.c)
    target_link_libraries(test_tokenizer PRIVATE hb_tokenizer)
    hb_apply_common(test_tokenizer)
    add_test(NAME tokenizer COMMAND test_tokenizer)
endif()

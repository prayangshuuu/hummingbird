# hb_quant — Quantization format registry: pack/unpack contracts and format detection.
add_library(hb_quant STATIC quant.c)
add_library(hummingbird::quant ALIAS hb_quant)

target_include_directories(hb_quant
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_quant PUBLIC hb_common hb_tensor)

hb_apply_common(hb_quant)

if(HB_BUILD_TESTS)
    add_executable(test_quant quant_test.c)
    target_link_libraries(test_quant PRIVATE hb_quant)
    hb_apply_common(test_quant)
    add_test(NAME quant COMMAND test_quant)
endif()

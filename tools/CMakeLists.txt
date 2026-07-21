# tools/ — offline, host-side utilities (not part of libhummingbird).
#
# Tools are standalone programs used at build/prep time, never on the inference
# hot path: the .hbm container converter, the token-exact oracle generator, and
# quality-ablation harnesses (see PROJECT_CONTEXT §2.1 tools/, DD-013). They may
# link the public library but must not be linked *by* it.
#
# PHASE 4: only a placeholder so the tree builds. Real tools arrive with M1+.

add_executable(hb_toolinfo toolinfo.c)
target_link_libraries(hb_toolinfo PRIVATE hummingbird::hummingbird)
hb_apply_common(hb_toolinfo)

if(HB_BUILD_TESTS)
    add_test(NAME tool_info COMMAND hb_toolinfo)
endif()

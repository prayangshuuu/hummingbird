# HBCompilerOptions.cmake — centralized compile/link policy.
#
# Every Hummingbird target (library, test, tool, example) calls
# hb_apply_common(<target>) so warning levels, standards, and sanitizer flags
# are defined in exactly one place. Do not set these per-target elsewhere.

include_guard(GLOBAL)

# Detect compiler families once.
set(HB_C_IS_MSVC OFF)
set(HB_C_IS_GNU_LIKE OFF)
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    set(HB_C_IS_MSVC ON)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set(HB_C_IS_GNU_LIKE ON)
endif()

# Assemble the warning flag list once.
set(HB_WARNING_FLAGS "")
if(HB_C_IS_MSVC)
    list(APPEND HB_WARNING_FLAGS /W4 /permissive-)
    if(HB_WARNINGS_AS_ERRORS)
        list(APPEND HB_WARNING_FLAGS /WX)
    endif()
elseif(HB_C_IS_GNU_LIKE)
    list(APPEND HB_WARNING_FLAGS
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wcast-qual -Wpointer-arith -Wwrite-strings
        -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition
        -Wvla)
    if(HB_WARNINGS_AS_ERRORS)
        list(APPEND HB_WARNING_FLAGS -Werror)
    endif()
endif()

# hb_apply_common(<target>)
#   Applies the shared warning set and sanitizer flags to a target.
function(hb_apply_common target)
    target_compile_options(${target} PRIVATE ${HB_WARNING_FLAGS})

    # Sanitizer flags are computed in HBSanitizers.cmake into HB_SANITIZE_*.
    if(HB_SANITIZE_COMPILE_FLAGS)
        target_compile_options(${target} PRIVATE ${HB_SANITIZE_COMPILE_FLAGS})
    endif()
    if(HB_SANITIZE_LINK_FLAGS)
        target_link_options(${target} PRIVATE ${HB_SANITIZE_LINK_FLAGS})
    endif()
endfunction()

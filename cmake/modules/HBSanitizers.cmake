# HBSanitizers.cmake — translate HB_ENABLE_ASAN / HB_ENABLE_UBSAN into flags.
#
# Produces two lists consumed by hb_apply_common():
#   HB_SANITIZE_COMPILE_FLAGS
#   HB_SANITIZE_LINK_FLAGS
#
# Sanitizers are a GCC/Clang feature here; on MSVC only /fsanitize=address is
# broadly available, so UBSan is a no-op there (warned once).

include_guard(GLOBAL)

set(HB_SANITIZE_COMPILE_FLAGS "" CACHE INTERNAL "sanitizer compile flags")
set(HB_SANITIZE_LINK_FLAGS    "" CACHE INTERNAL "sanitizer link flags")

if(NOT (HB_ENABLE_ASAN OR HB_ENABLE_UBSAN))
    return()
endif()

set(_san_list "")
if(HB_ENABLE_ASAN)
    list(APPEND _san_list address)
endif()
if(HB_ENABLE_UBSAN)
    list(APPEND _san_list undefined)
endif()
string(JOIN "," _san_csv ${_san_list})

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set(_flags -fsanitize=${_san_csv} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    set(HB_SANITIZE_COMPILE_FLAGS ${_flags} CACHE INTERNAL "" FORCE)
    set(HB_SANITIZE_LINK_FLAGS -fsanitize=${_san_csv} CACHE INTERNAL "" FORCE)
    message(STATUS "Sanitizers enabled: ${_san_csv}")
elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    if(HB_ENABLE_ASAN)
        set(HB_SANITIZE_COMPILE_FLAGS /fsanitize=address CACHE INTERNAL "" FORCE)
        message(STATUS "AddressSanitizer enabled (MSVC)")
    endif()
    if(HB_ENABLE_UBSAN)
        message(WARNING "UBSan is not supported by MSVC; ignoring HB_ENABLE_UBSAN.")
    endif()
endif()

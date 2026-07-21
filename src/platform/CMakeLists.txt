# hb_platform — Portability shim: OS/compiler/arch differences (file I/O, aligned alloc, threads, time) behind one API.
add_library(hb_platform STATIC platform.c)
add_library(hummingbird::platform ALIAS hb_platform)

target_include_directories(hb_platform
    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")

target_link_libraries(hb_platform PUBLIC hb_common)

# Threads: pthreads on POSIX, nothing extra on Windows (Win32 threads are in
# kernel32, linked by default). The platform module is the only place that needs
# the system threading library.
if(NOT WIN32)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(hb_platform PUBLIC Threads::Threads)
endif()

hb_apply_common(hb_platform)

if(HB_BUILD_TESTS)
    add_executable(test_platform platform_test.c)
    target_link_libraries(test_platform PRIVATE hb_platform hb_test_support)
    hb_apply_common(test_platform)
    add_test(NAME platform COMMAND test_platform)
endif()

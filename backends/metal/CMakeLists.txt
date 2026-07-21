# hb_backend_metal — optional Metal backend (scaffold).
#
# Built only when HB_BACKEND_METAL=ON, on Apple platforms. Kernels arrive in
# milestone M7; this is registration scaffolding only.

add_library(hb_backend_metal STATIC backend_metal.m)
add_library(hummingbird::backend_metal ALIAS hb_backend_metal)

target_include_directories(hb_backend_metal PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")
target_link_libraries(hb_backend_metal PUBLIC hb_common hb_backend)

find_library(METAL_FRAMEWORK Metal)
find_library(FOUNDATION_FRAMEWORK Foundation)
target_link_libraries(hb_backend_metal PUBLIC ${METAL_FRAMEWORK} ${FOUNDATION_FRAMEWORK})

/* backend_cuda.cu — optional CUDA backend (scaffold).
 *
 * Compiled only when -DHB_BACKEND_CUDA=ON. This translation unit is CUDA C++
 * but its only exported symbol is extern-C, so the C17 core links it through
 * the stable backend ABI exactly as it links the CPU backend (DD-007).
 *
 * No kernels yet — milestone M7. This file exists so enabling the option
 * produces a well-formed, registrable (no-op) backend rather than a build hole.
 */
extern "C" {
#include "backend/backend.h"
#include "backend_cuda.h"
}

static hbi_status cuda_init(void) {
    /* Device discovery/allocation arrives in M7; scaffold reports success. */
    return HBI_OK;
}

static void cuda_shutdown(void) {
}

static const hbi_backend g_cuda_backend = {
    /* .abi_version */ HBI_BACKEND_ABI_VERSION,
    /* .name        */ "cuda",
    /* .init        */ cuda_init,
    /* .shutdown    */ cuda_shutdown,
};

extern "C" hbi_status hb_backend_cuda_register(void) {
    return hbi_backend_register(&g_cuda_backend);
}

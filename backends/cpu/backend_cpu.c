/* backend_cpu.c — the always-present reference backend.
 *
 * The CPU backend is the correctness baseline: it is always compiled, always
 * available, and every other backend falls back to it per-tensor on any failure
 * (DD-007). This bootstrap provides only registration + identity; the quantized
 * SIMD kernels arrive in milestone M2 (see docs/architecture and PROJECT_CONTEXT
 * §6). It intentionally contains no inference math yet.
 */
#include "backend/backend.h"

#include "backend_cpu.h"
#include "backend_cpu_kernels.h"

/* On init the CPU backend registers its reference kernels into the kernel
 * runtime's registry (RFC-003, DD-025), so dispatch can find them. This runs
 * once, before worker threads, matching the registry's no-locking precondition. */
static hbi_status cpu_init(void) {
    return hb_backend_cpu_register_kernels();
}

static void cpu_shutdown(void) {
    /* Nothing to release in the scaffold. */
}

static const hbi_backend g_cpu_backend = {
    .abi_version = HBI_BACKEND_ABI_VERSION,
    .name = "cpu",
    .init = cpu_init,
    .shutdown = cpu_shutdown,
};

hbi_status hb_backend_cpu_register(void) {
    return hbi_backend_register(&g_cpu_backend);
}

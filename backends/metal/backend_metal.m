/* backend_metal.m — Apple Silicon Metal backend (stub).
 *
 * Optional backend, built only when HB_BACKEND_METAL=ON on macOS. This bootstrap
 * provides registration + identity only; the Metal shader pipeline and
 * unified-memory zero-copy residency arrive in milestone M7 (see PROJECT_CONTEXT
 * §6). It contains no inference math yet and must fall back to CPU per DD-007.
 *
 * Compiled as Objective-C (.m) so the real backend can host Objective-C++ later.
 */
#include "backend/backend.h"
#include "backend_metal.h"

static hbi_status metal_init(void) {
    return HBI_OK;
}

static void metal_shutdown(void) {
    /* Nothing to release in the stub. */
}

static const hbi_backend g_metal_backend = {
    .abi_version = HBI_BACKEND_ABI_VERSION,
    .name = "metal",
    .init = metal_init,
    .shutdown = metal_shutdown,
};

hbi_status hb_backend_metal_register(void) {
    return hbi_backend_register(&g_metal_backend);
}

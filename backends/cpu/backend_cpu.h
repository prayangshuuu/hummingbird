/* backend_cpu.h — registration entry point for the reference CPU backend.
 *
 * The CPU backend is always compiled and is the guaranteed fallback (DD-007).
 * The runtime calls hb_backend_cpu_register() during startup to add it to the
 * backend registry. Kept in its own header so the definition has a visible
 * prototype (-Wmissing-prototypes) and so the runtime can declare the dependency
 * without reaching into the backend's implementation file.
 */
#ifndef HB_BACKEND_CPU_H
#define HB_BACKEND_CPU_H

#include "common/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register the CPU backend with the core backend registry. Returns HBI_OK on
 * success. Call once, before worker threads start. */
hbi_status hb_backend_cpu_register(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_BACKEND_CPU_H */

/* backend_cpu_kernels.h — registration of the CPU reference kernels.
 *
 * The CPU backend owns the correctness-baseline scalar kernels for the kernel
 * runtime (RFC-003, DD-025). They register into the `kernel` module's registry
 * so dispatch can find them. Kept behind a prototype so the backend's init path
 * (and the unit test) can trigger registration without reaching into the .c.
 *
 * These are REFERENCE implementations: scalar, correctness-first, no SIMD. They
 * operate on C-contiguous fp32 operands (int8 also for copy/cast) and validate
 * every input, returning an hbi_status and never crashing on bad input.
 */
#ifndef HB_BACKEND_CPU_KERNELS_H
#define HB_BACKEND_CPU_KERNELS_H

#include "common/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register every CPU reference kernel with the kernel registry. Idempotent only
 * against a cleared registry — a second call without clearing collides
 * (HBI_ERR_STATE), which is the registry's no-silent-shadowing contract. Call
 * once at backend init, before worker threads start. */
hbi_status hb_backend_cpu_register_kernels(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_BACKEND_CPU_KERNELS_H */

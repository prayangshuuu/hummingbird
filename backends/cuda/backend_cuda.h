/* backend_cuda.h — registration entry point for the optional CUDA backend.
 *
 * Declared with C linkage so the C17 core can call it even though the backend
 * is compiled as CUDA C++ (.cu). Registers the CUDA vtable against the backend
 * ABI (DD-007). Only present when HB_BACKEND_CUDA=ON.
 */
#ifndef HB_BACKEND_CUDA_H
#define HB_BACKEND_CUDA_H

#include "backend/backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register the CUDA backend. Returns HBI_OK on success. */
hbi_status hb_backend_cuda_register(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_BACKEND_CUDA_H */

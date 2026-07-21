/* backend_metal.h — registration entry point for the optional Metal backend.
 *
 * Declared with C linkage so the C17 core can call it from the module tree.
 * Registers the Metal vtable against the backend ABI (DD-007). Only present
 * when HB_BACKEND_METAL=ON on Apple platforms.
 */
#ifndef HB_BACKEND_METAL_H
#define HB_BACKEND_METAL_H

#include "backend/backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register the Metal backend. Returns HBI_OK on success. */
hbi_status hb_backend_metal_register(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_BACKEND_METAL_H */

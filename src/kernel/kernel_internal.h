/* kernel_internal.h — private to the `kernel` module.
 *
 * Nothing here is visible to other modules. The registry table and any private
 * helpers live here; kernel.c is the only translation unit that includes it.
 */
#ifndef HB_KERNEL_INTERNAL_H
#define HB_KERNEL_INTERNAL_H

#include "kernel/kernel.h"

/* Registry capacity. Generous for the bootstrap: a handful of backends, each
 * providing one kernel per (op, dtype). Dynamic growth arrives with plug-in
 * loading (DD-007). Registration happens once at init before threads, so a
 * fixed table needs no locking. */
enum { HBI_KERNEL_REGISTRY_MAX = 64 };

#endif /* HB_KERNEL_INTERNAL_H */

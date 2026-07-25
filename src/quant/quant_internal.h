/* quant_internal.h — private to the `quant` module.
 *
 * Nothing here is visible to other modules. Implementation details,
 * internal structs, and static-helper prototypes live here as the module grows.
 */
#ifndef HB_QUANT_INTERNAL_H
#define HB_QUANT_INTERNAL_H

#include "quant/quant.h"

#include <stdbool.h>
#include <stdint.h>

/* All decode helpers are file-static in quant.c; nothing crosses the boundary
 * yet beyond the public API. Declarations land here if the module is split. */

#endif /* HB_QUANT_INTERNAL_H */

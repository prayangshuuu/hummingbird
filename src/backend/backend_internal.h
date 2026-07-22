/* backend_internal.h — private to the `backend` module.
 *
 * Nothing here is visible to other modules. Implementation details,
 * internal structs, and static-helper prototypes live here as the module grows.
 */
#ifndef HB_BACKEND_INTERNAL_H
#define HB_BACKEND_INTERNAL_H

#include "backend/backend.h"
#include <string.h>

/* The manager tracks active backend contexts for a single runtime session. */
struct hbi_backend_manager {
    hbi_allocator *allocator;

    /* Active contexts cached for each registered backend.
     * We limit this to the maximum number of backends (small fixed size). */
    hbi_backend_context *contexts[8]; /* Assuming HBI_BACKEND_MAX is 8 */
};

#endif /* HB_BACKEND_INTERNAL_H */

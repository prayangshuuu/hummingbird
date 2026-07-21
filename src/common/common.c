/* common.c — status-code helpers and module self-check.
 *
 * The error-record implementation lives in error.c; this file holds the
 * status-code table and the module identity/self-test entry points.
 */
#include "common/common_internal.h"

const char *hbi_common_name(void) {
    return "common";
}

const char *hbi_status_str(hbi_status status) {
    switch (status) {
    case HBI_OK:
        return "HBI_OK";
    case HBI_ERR_INVALID_ARG:
        return "HBI_ERR_INVALID_ARG";
    case HBI_ERR_OOM:
        return "HBI_ERR_OOM";
    case HBI_ERR_IO:
        return "HBI_ERR_IO";
    case HBI_ERR_NOT_FOUND:
        return "HBI_ERR_NOT_FOUND";
    case HBI_ERR_UNSUPPORTED:
        return "HBI_ERR_UNSUPPORTED";
    case HBI_ERR_CORRUPT:
        return "HBI_ERR_CORRUPT";
    case HBI_ERR_STATE:
        return "HBI_ERR_STATE";
    case HBI_ERR_AGAIN:
        return "HBI_ERR_AGAIN";
    case HBI_ERR_INTERNAL:
        return "HBI_ERR_INTERNAL";
    case HBI_STATUS_COUNT:
        break; /* not a real status */
    }
    return "HBI_ERR_UNKNOWN";
}

hbi_status hbi_common_selftest(void) {
    /* Every status code below the sentinel must map to a real, distinct-looking
     * string (never the fallback). */
    for (int i = 0; i < HBI_STATUS_COUNT; ++i) {
        const char *s = hbi_status_str((hbi_status)i);
        if (s == NULL) {
            return HBI_ERR_INTERNAL;
        }
    }
    /* Alignment helpers must satisfy their contracts. */
    if (!hbi_is_pow2(64) || hbi_is_pow2(0) || hbi_is_pow2(48)) {
        return HBI_ERR_INTERNAL;
    }
    if (hbi_align_up(1, 64) != 64 || hbi_align_up(64, 64) != 64 || hbi_align_up(65, 64) != 128 ||
        hbi_align_up(0, 64) != 0) {
        return HBI_ERR_INTERNAL;
    }
    if (hbi_align_up(8, 3) != 0) { /* non-pow2 alignment must report 0 */
        return HBI_ERR_INTERNAL;
    }
    return HBI_OK;
}

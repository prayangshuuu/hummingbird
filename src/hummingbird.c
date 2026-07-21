/* hummingbird.c — implementation of the stable public C ABI.
 *
 * This translation unit is the bridge between the installed public header
 * (<hummingbird/hummingbird.h>) and the internal engine modules (hbi_*). It is
 * the only place allowed to translate internal `hb_status` values into the
 * stable public `hb_status` numbering. Keep it thin: real work lives in the
 * modules under src/.
 */
#include "hummingbird/hummingbird.h"

#include "common/common.h"
#include "runtime/runtime.h"

#include <stddef.h>

const char *hb_version_string(void) {
    return "0.0.0";
}

int hb_version(void) {
    return HB_VERSION;
}

const char *hb_status_string(hb_status status) {
    switch (status) {
    case HB_OK:
        return "HB_OK";
    case HB_ERR_UNKNOWN:
        return "HB_ERR_UNKNOWN";
    case HB_ERR_INVALID_ARG:
        return "HB_ERR_INVALID_ARG";
    case HB_ERR_NO_MEMORY:
        return "HB_ERR_NO_MEMORY";
    case HB_ERR_IO:
        return "HB_ERR_IO";
    case HB_ERR_NOT_FOUND:
        return "HB_ERR_NOT_FOUND";
    case HB_ERR_UNSUPPORTED:
        return "HB_ERR_UNSUPPORTED";
    case HB_ERR_CORRUPT:
        return "HB_ERR_CORRUPT";
    case HB_ERR_NOT_IMPLEMENTED:
        return "HB_ERR_NOT_IMPLEMENTED";
    }
    return "HB_ERR_UNKNOWN";
}

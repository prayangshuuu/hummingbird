/* context.c — Per-session decode context: KV state, sampling state, run mode. */
#include "context/context_internal.h"

const char *hbi_context_name(void) {
    return "context";
}

hbi_status hbi_context_selftest(void) {
    /* Scaffold: no invariants to check yet. */
    return HBI_OK;
}

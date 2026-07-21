/* executor.c — Walks a forward graph and dispatches each op node to its typed module on the active
 * backend. */
#include "executor/executor_internal.h"

const char *hbi_executor_name(void) {
    return "executor";
}

hbi_status hbi_executor_selftest(void) {
    /* Scaffold: no invariants to check yet. */
    return HBI_OK;
}

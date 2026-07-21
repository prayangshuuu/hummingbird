/* runtime.c — Orchestrator: owns the forward loop, sequences layers, drives the scheduler, produces
 * logits. */
#include "runtime/runtime_internal.h"

const char *hbi_runtime_name(void) {
    return "runtime";
}

hbi_status hbi_runtime_selftest(void) {
    /* Scaffold: no invariants to check yet. */
    return HBI_OK;
}

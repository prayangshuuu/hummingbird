/* scheduler.c — Overlaps I/O with compute and prefetches (PIPE/PILOT). Speculative actions never
 * change output. */
#include "scheduler/scheduler_internal.h"

const char *hbi_scheduler_name(void) {
    return "scheduler";
}

hbi_status hbi_scheduler_selftest(void) {
    /* Scaffold: no invariants to check yet. */
    return HBI_OK;
}

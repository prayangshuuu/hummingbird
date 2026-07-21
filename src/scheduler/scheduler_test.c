/* scheduler_test.c — unit-test placeholder for the `scheduler` module.
 * Replace with real cases as the module gains behavior. */
#include "scheduler/scheduler.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    if (hbi_scheduler_selftest() != HBI_OK) {
        fprintf(stderr, "%s: selftest failed\n", hbi_scheduler_name());
        return 1;
    }
    if (strcmp(hbi_scheduler_name(), "scheduler") != 0) {
        fprintf(stderr, "unexpected module name: %s\n", hbi_scheduler_name());
        return 1;
    }
    printf("[ok] %s\n", hbi_scheduler_name());
    return 0;
}

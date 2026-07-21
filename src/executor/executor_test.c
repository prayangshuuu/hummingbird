/* executor_test.c — unit-test placeholder for the `executor` module.
 * Replace with real cases as the module gains behavior. */
#include "executor/executor.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    if (hbi_executor_selftest() != HBI_OK) {
        fprintf(stderr, "%s: selftest failed\n", hbi_executor_name());
        return 1;
    }
    if (strcmp(hbi_executor_name(), "executor") != 0) {
        fprintf(stderr, "unexpected module name: %s\n", hbi_executor_name());
        return 1;
    }
    printf("[ok] %s\n", hbi_executor_name());
    return 0;
}

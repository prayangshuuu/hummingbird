/* runtime_test.c — unit-test placeholder for the `runtime` module.
 * Replace with real cases as the module gains behavior. */
#include "runtime/runtime.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    if (hbi_runtime_selftest() != HBI_OK) {
        fprintf(stderr, "%s: selftest failed\n", hbi_runtime_name());
        return 1;
    }
    if (strcmp(hbi_runtime_name(), "runtime") != 0) {
        fprintf(stderr, "unexpected module name: %s\n", hbi_runtime_name());
        return 1;
    }
    printf("[ok] %s\n", hbi_runtime_name());
    return 0;
}

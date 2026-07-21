/* context_test.c — unit-test placeholder for the `context` module.
 * Replace with real cases as the module gains behavior. */
#include "context/context.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    if (hbi_context_selftest() != HBI_OK) {
        fprintf(stderr, "%s: selftest failed\n", hbi_context_name());
        return 1;
    }
    if (strcmp(hbi_context_name(), "context") != 0) {
        fprintf(stderr, "unexpected module name: %s\n", hbi_context_name());
        return 1;
    }
    printf("[ok] %s\n", hbi_context_name());
    return 0;
}

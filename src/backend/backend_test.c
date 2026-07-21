/* backend_test.c — unit-test placeholder for the `backend` module.
 * Replace with real cases as the module gains behavior. */
#include "backend/backend.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    if (hbi_backend_selftest() != HBI_OK) {
        fprintf(stderr, "%s: selftest failed\n", hbi_backend_name());
        return 1;
    }
    if (strcmp(hbi_backend_name(), "backend") != 0) {
        fprintf(stderr, "unexpected module name: %s\n", hbi_backend_name());
        return 1;
    }
    printf("[ok] %s\n", hbi_backend_name());
    return 0;
}

/* kv_test.c — unit-test placeholder for the `kv` module.
 * Replace with real cases as the module gains behavior. */
#include "kv/kv.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    if (hbi_kv_selftest() != HBI_OK) {
        fprintf(stderr, "%s: selftest failed\n", hbi_kv_name());
        return 1;
    }
    if (strcmp(hbi_kv_name(), "kv") != 0) {
        fprintf(stderr, "unexpected module name: %s\n", hbi_kv_name());
        return 1;
    }
    printf("[ok] %s\n", hbi_kv_name());
    return 0;
}

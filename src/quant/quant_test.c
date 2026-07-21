/* quant_test.c — unit-test placeholder for the `quant` module.
 * Replace with real cases as the module gains behavior. */
#include "quant/quant.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    if (hbi_quant_selftest() != HBI_OK) {
        fprintf(stderr, "%s: selftest failed\n", hbi_quant_name());
        return 1;
    }
    if (strcmp(hbi_quant_name(), "quant") != 0) {
        fprintf(stderr, "unexpected module name: %s\n", hbi_quant_name());
        return 1;
    }
    printf("[ok] %s\n", hbi_quant_name());
    return 0;
}

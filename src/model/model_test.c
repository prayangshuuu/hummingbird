/* model_test.c — unit-test placeholder for the `model` module.
 * Replace with real cases as the module gains behavior. */
#include "model/model.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    if (hbi_model_selftest() != HBI_OK) {
        fprintf(stderr, "%s: selftest failed\n", hbi_model_name());
        return 1;
    }
    if (strcmp(hbi_model_name(), "model") != 0) {
        fprintf(stderr, "unexpected module name: %s\n", hbi_model_name());
        return 1;
    }
    printf("[ok] %s\n", hbi_model_name());
    return 0;
}

/* tokenizer_test.c — unit-test placeholder for the `tokenizer` module.
 * Replace with real cases as the module gains behavior. */
#include "tokenizer/tokenizer.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    if (hbi_tokenizer_selftest() != HBI_OK) {
        fprintf(stderr, "%s: selftest failed\n", hbi_tokenizer_name());
        return 1;
    }
    if (strcmp(hbi_tokenizer_name(), "tokenizer") != 0) {
        fprintf(stderr, "unexpected module name: %s\n", hbi_tokenizer_name());
        return 1;
    }
    printf("[ok] %s\n", hbi_tokenizer_name());
    return 0;
}

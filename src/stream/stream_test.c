/* stream_test.c — unit-test placeholder for the `stream` module.
 * Replace with real cases as the module gains behavior. */
#include "stream/stream.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    if (hbi_stream_selftest() != HBI_OK) {
        fprintf(stderr, "%s: selftest failed\n", hbi_stream_name());
        return 1;
    }
    if (strcmp(hbi_stream_name(), "stream") != 0) {
        fprintf(stderr, "unexpected module name: %s\n", hbi_stream_name());
        return 1;
    }
    printf("[ok] %s\n", hbi_stream_name());
    return 0;
}

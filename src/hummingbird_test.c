/* hummingbird_test.c — smoke test for the public ABI.
 * Exercises the installed header exactly as an external embedder would. */
#include "hummingbird/hummingbird.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    if (hb_version() != HB_VERSION) {
        fprintf(stderr, "version mismatch: %d vs %d\n", hb_version(), HB_VERSION);
        return 1;
    }
    if (hb_version_string() == NULL || strlen(hb_version_string()) == 0) {
        fprintf(stderr, "empty version string\n");
        return 1;
    }
    if (strcmp(hb_status_string(HB_OK), "HB_OK") != 0) {
        fprintf(stderr, "bad status string for HB_OK\n");
        return 1;
    }
    printf("[ok] libhummingbird %s (ABI %d)\n", hb_version_string(), hb_version());
    return 0;
}

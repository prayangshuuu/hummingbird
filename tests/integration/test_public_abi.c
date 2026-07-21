/* test_public_abi.c — integration smoke test for the stable public ABI.
 *
 * Links libhummingbird through the installed public header exactly as an
 * external embedder would, and asserts the version/status surface behaves.
 * This is not a unit test of any single module — it proves the aggregate
 * library links and its public entry points are callable. */
#include <hummingbird/hummingbird.h>

#include <stdio.h>
#include <string.h>

int main(void) {
    /* Packed version matches the compile-time macro the caller built against. */
    if (hb_version() != HB_VERSION) {
        fprintf(stderr, "version mismatch: runtime %d vs header %d\n", hb_version(), HB_VERSION);
        return 1;
    }

    /* Version string is present and non-empty. */
    const char *vs = hb_version_string();
    if (vs == NULL || strlen(vs) == 0) {
        fprintf(stderr, "empty version string\n");
        return 1;
    }

    /* Status strings are stable and never NULL. */
    if (strcmp(hb_status_string(HB_OK), "HB_OK") != 0) {
        fprintf(stderr, "bad status string for HB_OK: %s\n", hb_status_string(HB_OK));
        return 1;
    }
    if (hb_status_string(HB_ERR_NOT_IMPLEMENTED) == NULL) {
        fprintf(stderr, "NULL status string\n");
        return 1;
    }

    printf("[ok] public ABI: libhummingbird %s (v%d)\n", vs, hb_version());
    return 0;
}

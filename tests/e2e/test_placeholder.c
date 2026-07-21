/* test_placeholder.c — end-to-end tree placeholder.
 *
 * The token-exact oracle gate (the project's non-negotiable correctness check,
 * PROJECT_CONTEXT §12) will run here: load a tiny model, generate under
 * teacher forcing, and compare logits/tokens bit-for-bit against a reference.
 * That requires model loading + a forward pass (milestones M1–M3); until then
 * this placeholder keeps the e2e target wired into CI.
 */
#include <hummingbird/hummingbird.h>

#include <stdio.h>
#include <string.h>

int main(void) {
    if (hb_version_string() == NULL || strlen(hb_version_string()) == 0) {
        fprintf(stderr, "no version string\n");
        return 1;
    }
    printf("[ok] e2e harness (libhummingbird %s)\n", hb_version_string());
    return 0;
}

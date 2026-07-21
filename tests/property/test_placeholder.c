/* test_placeholder.c — property-test tree placeholder.
 *
 * The real property tests assert engine invariants that must hold for ALL
 * inputs, most importantly the streaming coalesced-read / zero-copy contract
 * (ADR DD-005): an expert's gate/up/down sub-tensors are laid adjacent on disk
 * and loaded in a single read, with the tensor views pointing into that one
 * slab. That test lands with the streaming engine (milestone M4).
 *
 * Until then this asserts the property harness itself compiles and runs.
 */
#include <hummingbird/hummingbird.h>

#include <stdio.h>

int main(void) {
    /* Invariant (trivial, bootstrap): the status enum's success value is 0, so
     * `if (status)` means "error" everywhere in the codebase. */
    if ((int)HB_OK != 0) {
        fprintf(stderr, "invariant violated: HB_OK must be 0\n");
        return 1;
    }
    printf("[ok] property harness\n");
    return 0;
}

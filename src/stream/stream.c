/* stream.c — Weight streaming: contiguity to single coalesced read to zero-copy views (hard
 * invariant). */
#include "stream/stream_internal.h"

const char *hbi_stream_name(void) {
    return "stream";
}

hbi_status hbi_stream_selftest(void) {
    /* Scaffold: no invariants to check yet. */
    return HBI_OK;
}

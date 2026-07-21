/* kv.c — KV-cache abstraction: MHA/GQA and compressed-latent (MLA) layouts; save/restore. */
#include "kv/kv_internal.h"

const char *hbi_kv_name(void) {
    return "kv";
}

hbi_status hbi_kv_selftest(void) {
    /* Scaffold: no invariants to check yet. */
    return HBI_OK;
}

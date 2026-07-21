/* tokenizer.c — Tokenizer adapter interface (BPE/unigram/...); encode/decode. */
#include "tokenizer/tokenizer_internal.h"

const char *hbi_tokenizer_name(void) {
    return "tokenizer";
}

hbi_status hbi_tokenizer_selftest(void) {
    /* Scaffold: no invariants to check yet. */
    return HBI_OK;
}

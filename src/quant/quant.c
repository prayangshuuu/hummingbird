/* quant.c — Quantization format registry: pack/unpack contracts and format detection. */
#include "quant/quant_internal.h"

const char *hbi_quant_name(void) {
    return "quant";
}

hbi_status hbi_quant_selftest(void) {
    /* Scaffold: no invariants to check yet. */
    return HBI_OK;
}

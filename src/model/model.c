/* model.c — Model adapter: descriptor + forward graph + tokenizer ref + quant classes. New models
 * are additive. */
#include "model/model_internal.h"

const char *hbi_model_name(void) {
    return "model";
}

hbi_status hbi_model_selftest(void) {
    /* Scaffold: no invariants to check yet. */
    return HBI_OK;
}

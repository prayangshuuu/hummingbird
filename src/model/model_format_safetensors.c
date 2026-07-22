/* model_format_safetensors.c — skeleton Safetensors format handler.
 *
 * Detects Safetensors files by checking that the first 8 bytes are a
 * reasonable little-endian u64 header length (< 100 MB). Full parsing is
 * deferred to a later RFC.
 */
#include "model/model_internal.h"

#include <stdio.h>
#include <string.h>

/* Safetensors: first 8 bytes are a little-endian u64 giving the JSON
 * header length. A reasonable upper bound is 100 MB. */
#define SAFETENSORS_MAX_HEADER (100u * 1024u * 1024u)

static uint64_t read_le_u64(const uint8_t *p) {
    return ((uint64_t)p[0]) | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

static bool safetensors_detect(const char *path) {
    if (!path) {
        return false;
    }
    /* Quick extension check. */
    const char *ext = strrchr(path, '.');
    if (!ext || strcmp(ext, ".safetensors") != 0) {
        return false;
    }
    /* Validate the header length prefix. */
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    uint8_t buf[8] = {0};
    size_t n = fread(buf, 1, 8, f);
    fclose(f);
    if (n < 8) {
        return false;
    }
    uint64_t header_len = read_le_u64(buf);
    /* A valid safetensors file has a non-zero, reasonable header length. */
    return header_len > 0 && header_len < SAFETENSORS_MAX_HEADER;
}

static hbi_status safetensors_parse_metadata(const char *path, hbi_allocator *allocator,
                                             hbi_model_manifest *manifest,
                                             hbi_model_metadata *metadata) {
    HB_UNUSED(path);
    HB_UNUSED(allocator);
    HB_UNUSED(manifest);
    HB_UNUSED(metadata);
    return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "Safetensors parser not yet implemented (skeleton)");
}

static hbi_status safetensors_read_tensor_data(const char *path, const hbi_tensor_entry *entry,
                                               void *buf, size_t buf_size) {
    HB_UNUSED(path);
    HB_UNUSED(entry);
    HB_UNUSED(buf);
    HB_UNUSED(buf_size);
    return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "Safetensors reader not yet implemented (skeleton)");
}

static const hbi_format_handler g_safetensors_handler = {
    .name = "safetensors",
    .format = HBI_MODEL_FORMAT_SAFETENSORS,
    .detect = safetensors_detect,
    .parse_metadata = safetensors_parse_metadata,
    .read_tensor_data = safetensors_read_tensor_data,
};

hbi_status hbi_format_safetensors_register(void) {
    return hbi_format_handler_register(&g_safetensors_handler);
}

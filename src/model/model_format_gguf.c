/* model_format_gguf.c — skeleton GGUF format handler.
 *
 * Detects GGUF files by their magic bytes. Full parsing is deferred to a
 * later RFC; parse_metadata and read_tensor_data return HBI_ERR_UNSUPPORTED.
 */
#include "model/model_internal.h"

#include <stdio.h>
#include <string.h>

/* GGUF magic: ASCII "GGUF" = 0x46475547 little-endian. */
static const uint8_t GGUF_MAGIC[4] = {0x47, 0x47, 0x55, 0x46};

static bool gguf_detect(const char *path) {
    if (!path) {
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    uint8_t buf[4] = {0};
    size_t n = fread(buf, 1, 4, f);
    fclose(f);
    if (n < 4) {
        return false;
    }
    return memcmp(buf, GGUF_MAGIC, 4) == 0;
}

static hbi_status gguf_parse_metadata(const char *path, hbi_allocator *allocator,
                                      hbi_model_manifest *manifest, hbi_model_metadata *metadata) {
    HB_UNUSED(path);
    HB_UNUSED(allocator);
    HB_UNUSED(manifest);
    HB_UNUSED(metadata);
    return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "GGUF parser not yet implemented (skeleton)");
}

static hbi_status gguf_read_tensor_data(const char *path, const hbi_tensor_entry *entry, void *buf,
                                        size_t buf_size) {
    HB_UNUSED(path);
    HB_UNUSED(entry);
    HB_UNUSED(buf);
    HB_UNUSED(buf_size);
    return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "GGUF reader not yet implemented (skeleton)");
}

static const hbi_format_handler g_gguf_handler = {
    .name = "gguf",
    .format = HBI_MODEL_FORMAT_GGUF,
    .detect = gguf_detect,
    .parse_metadata = gguf_parse_metadata,
    .read_tensor_data = gguf_read_tensor_data,
};

hbi_status hbi_format_gguf_register(void) {
    return hbi_format_handler_register(&g_gguf_handler);
}

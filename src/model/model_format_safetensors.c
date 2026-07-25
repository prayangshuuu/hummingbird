/* model_format_safetensors.c — Safetensors format handler (RFC-015).
 *
 * Format layout:
 *   [8 bytes little-endian u64 header_len]
 *   [header_len bytes of UTF-8 JSON]
 *   [tensor data region]
 *
 * The JSON header is an object mapping tensor name → {dtype, shape, data_offsets}
 * plus an optional "__metadata__" object of string key/values. `data_offsets`
 * are [begin, end) relative to the START of the data region (i.e. byte 0 is the
 * first byte after the header), per the safetensors spec.
 *
 * This handler is deliberately QUANT-AGNOSTIC (DD-036): it maps safetensors
 * dtype strings to hbi_dtype mechanically and indexes byte ranges. It has no
 * knowledge of MXFP4 block/scale pairing — that is the quant module's job,
 * invoked later by the adapter. MXFP4 weights ship as ordinary U8 `*_blocks`
 * and `*_scales` tensors here.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif
#include "model/model_internal.h"

#include "json/json.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

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
    const char *ext = strrchr(path, '.');
    if (!ext || strcmp(ext, ".safetensors") != 0) {
        return false;
    }
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
    return header_len > 0 && header_len < SAFETENSORS_MAX_HEADER;
}

/* Map a safetensors dtype string to an hbi_dtype. Sub-byte / packed formats
 * (MXFP4) ship as byte containers (U8) and are mapped to INT8 — the quant module
 * reinterprets the bytes later. Returns HBI_DTYPE_INVALID for unknown strings. */
static hbi_dtype map_dtype(const char *s) {
    if (!s) {
        return HBI_DTYPE_INVALID;
    }
    if (strcmp(s, "F32") == 0) {
        return HBI_DTYPE_FP32;
    }
    if (strcmp(s, "F16") == 0) {
        return HBI_DTYPE_FP16;
    }
    if (strcmp(s, "BF16") == 0) {
        return HBI_DTYPE_BF16;
    }
    /* Byte containers: I8/U8/BOOL all index as raw bytes. */
    if (strcmp(s, "I8") == 0 || strcmp(s, "U8") == 0 || strcmp(s, "BOOL") == 0) {
        return HBI_DTYPE_INT8;
    }
    return HBI_DTYPE_INVALID;
}

/* Read the whole JSON header into a freshly malloc'd buffer. Caller frees.
 * Writes the header length (and thus the data-region base = 8 + header_len). */
static hbi_status read_header(const char *path, char **out_json, uint64_t *out_header_len,
                              uint64_t *out_file_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return HBI_ERR_SETF(HBI_ERR_IO, 0, "safetensors: cannot open %s", path);
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return HBI_ERR_SET(HBI_ERR_IO, 0, "safetensors: seek end failed");
    }
    long file_size = ftell(f);
    if (file_size < 8) {
        fclose(f);
        return HBI_ERR_SET(HBI_ERR_CORRUPT, 0, "safetensors: file too small");
    }
    rewind(f);

    uint8_t lenbuf[8] = {0};
    if (fread(lenbuf, 1, 8, f) != 8) {
        fclose(f);
        return HBI_ERR_SET(HBI_ERR_IO, 0, "safetensors: cannot read header length");
    }
    uint64_t header_len = read_le_u64(lenbuf);
    if (header_len == 0 || header_len >= SAFETENSORS_MAX_HEADER ||
        (uint64_t)file_size < 8 + header_len) {
        fclose(f);
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "safetensors: bad header length %" PRIu64,
                            header_len);
    }

    char *json = (char *)malloc(header_len);
    if (!json) {
        fclose(f);
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "safetensors: header alloc failed");
    }
    if (fread(json, 1, header_len, f) != header_len) {
        free(json);
        fclose(f);
        return HBI_ERR_SET(HBI_ERR_IO, 0, "safetensors: short header read");
    }
    fclose(f);

    *out_json = json;
    *out_header_len = header_len;
    *out_file_size = (uint64_t)file_size;
    return HBI_OK;
}

/* Parse one tensor object: {"dtype":..,"shape":[..],"data_offsets":[b,e]}. */
static hbi_status parse_tensor_entry(const char *name, const hbi_json_value *obj,
                                     uint64_t data_base, uint64_t file_size,
                                     hbi_tensor_entry *out) {
    const hbi_json_value *dt = hbi_json_object_get(obj, "dtype");
    const hbi_json_value *shape = hbi_json_object_get(obj, "shape");
    const hbi_json_value *offs = hbi_json_object_get(obj, "data_offsets");
    if (!hbi_json_is_string(dt) || !hbi_json_is_array(shape) || !hbi_json_is_array(offs)) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "safetensors: tensor '%s' missing fields", name);
    }

    hbi_dtype dtype = map_dtype(hbi_json_as_string(dt));
    if (dtype == HBI_DTYPE_INVALID) {
        return HBI_ERR_SETF(HBI_ERR_UNSUPPORTED, 0, "safetensors: tensor '%s' dtype '%s'", name,
                            hbi_json_as_string(dt));
    }

    /* Shape (a 0-d scalar is stored as an empty shape array). */
    size_t rank = hbi_json_array_count(shape);
    if (rank > HBI_TENSOR_MAX_RANK) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "safetensors: tensor '%s' rank %zu too large", name,
                            rank);
    }
    int64_t dims[HBI_TENSOR_MAX_RANK];
    for (size_t i = 0; i < rank; i++) {
        int64_t d = 0;
        if (hbi_json_as_int(hbi_json_array_at(shape, i), &d) != HBI_OK || d <= 0) {
            return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "safetensors: tensor '%s' bad dim %zu", name,
                                i);
        }
        dims[i] = d;
    }
    hbi_shape sh;
    hbi_status st;
    if (rank == 0) {
        st = hbi_shape_init_scalar(&sh);
    } else {
        st = hbi_shape_init(&sh, dims, (uint32_t)rank);
    }
    if (st != HBI_OK) {
        return st;
    }

    /* data_offsets = [begin, end) relative to the data region base. */
    if (hbi_json_array_count(offs) != 2) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "safetensors: tensor '%s' bad data_offsets", name);
    }
    int64_t begin = 0, end = 0;
    if (hbi_json_as_int(hbi_json_array_at(offs, 0), &begin) != HBI_OK ||
        hbi_json_as_int(hbi_json_array_at(offs, 1), &end) != HBI_OK || begin < 0 || end < begin) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "safetensors: tensor '%s' offset range", name);
    }
    uint64_t abs_begin = data_base + (uint64_t)begin;
    uint64_t byte_size = (uint64_t)(end - begin);
    if (abs_begin + byte_size > file_size) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0,
                            "safetensors: tensor '%s' extends past EOF (%" PRIu64 " > %" PRIu64 ")",
                            name, abs_begin + byte_size, file_size);
    }

    memset(out, 0, sizeof(*out));
    size_t name_len = strlen(name);
    if (name_len >= HBI_TENSOR_NAME_MAX) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "safetensors: tensor name too long (%zu)",
                            name_len);
    }
    memcpy(out->name, name, name_len + 1);
    out->shape = sh;
    out->dtype = dtype;
    out->file_offset = abs_begin;
    out->byte_size = byte_size;
    out->required_alignment = hbi_dtype_align(dtype);
    out->shard_index = 0;
    out->residency = HBI_RESIDENCY_DEFAULT;
    return HBI_OK;
}

static hbi_status safetensors_parse_metadata(const char *path, hbi_allocator *allocator,
                                             hbi_model_manifest *manifest,
                                             hbi_model_metadata *metadata) {
    HB_UNUSED(allocator);
    if (!path || !manifest) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "safetensors_parse: NULL arg");
    }

    char *json = NULL;
    uint64_t header_len = 0, file_size = 0;
    hbi_status st = read_header(path, &json, &header_len, &file_size);
    if (st != HBI_OK) {
        return st;
    }
    uint64_t data_base = 8u + header_len;

    hbi_json_value *root = NULL;
    st = hbi_json_parse(json, (size_t)header_len, hbi_allocator_system(), &root);
    if (st != HBI_OK) {
        free(json);
        return st;
    }
    if (!hbi_json_is_object(root)) {
        hbi_json_free(root, hbi_allocator_system());
        free(json);
        return HBI_ERR_SET(HBI_ERR_CORRUPT, 0, "safetensors: header is not a JSON object");
    }

    size_t nmembers = hbi_json_object_count(root);
    for (size_t i = 0; i < nmembers; i++) {
        const char *key = NULL;
        const hbi_json_value *val = NULL;
        if (!hbi_json_object_member_at(root, i, &key, &val)) {
            continue;
        }
        if (strcmp(key, "__metadata__") == 0) {
            /* Optional string key/value map — copy into model metadata. */
            if (metadata && hbi_json_is_object(val)) {
                size_t mcount = hbi_json_object_count(val);
                for (size_t j = 0; j < mcount; j++) {
                    const char *mk = NULL;
                    const hbi_json_value *mv = NULL;
                    if (hbi_json_object_member_at(val, j, &mk, &mv) && hbi_json_is_string(mv)) {
                        (void)hbi_model_metadata_set(metadata, mk, hbi_json_as_string(mv));
                    }
                }
            }
            continue;
        }
        if (!hbi_json_is_object(val)) {
            continue; /* skip non-tensor entries defensively */
        }
        hbi_tensor_entry entry;
        st = parse_tensor_entry(key, val, data_base, file_size, &entry);
        if (st != HBI_OK) {
            hbi_json_free(root, hbi_allocator_system());
            free(json);
            return st;
        }
        st = hbi_model_manifest_add(manifest, &entry);
        if (st != HBI_OK) {
            hbi_json_free(root, hbi_allocator_system());
            free(json);
            return st;
        }
    }

    hbi_json_free(root, hbi_allocator_system());
    free(json);
    return HBI_OK;
}

static hbi_status safetensors_read_tensor_data(const char *path, const hbi_tensor_entry *entry,
                                               void *buf, size_t buf_size) {
    if (!path || !entry || !buf) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "safetensors_read: NULL arg");
    }
    if (buf_size < entry->byte_size) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0,
                            "safetensors_read: buffer %zu < tensor %" PRIu64, buf_size,
                            entry->byte_size);
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return HBI_ERR_SETF(HBI_ERR_IO, 0, "safetensors_read: cannot open %s", path);
    }
    /* Absolute file offset was pre-resolved (8 + header_len + local offset). */
#if defined(_WIN32)
    if (_fseeki64(f, (long long)entry->file_offset, SEEK_SET) != 0) {
#else
    if (fseeko(f, (off_t)entry->file_offset, SEEK_SET) != 0) {
#endif
        fclose(f);
        return HBI_ERR_SET(HBI_ERR_IO, 0, "safetensors_read: seek failed");
    }
    size_t n = fread(buf, 1, entry->byte_size, f);
    fclose(f);
    if (n != entry->byte_size) {
        return HBI_ERR_SETF(HBI_ERR_IO, 0, "safetensors_read: short read (%zu/%" PRIu64 ")", n,
                            entry->byte_size);
    }
    return HBI_OK;
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

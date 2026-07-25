/* json_internal.h — private declarations for the json module.
 *
 * Only json.c includes this. It exposes the parser-state struct and helpers so
 * the implementation can be split if it grows, without widening the public API.
 */
#ifndef HB_JSON_INTERNAL_H
#define HB_JSON_INTERNAL_H

#include "json/json.h"

#include <stddef.h>

/* Parser cursor over the input buffer. */
typedef struct hbi_json_parser {
    const char *buf;      /* input start */
    size_t len;           /* input length */
    size_t pos;           /* current byte offset */
    uint32_t depth;       /* current nesting depth */
    hbi_allocator *alloc; /* DOM allocator */
} hbi_json_parser;

#endif /* HB_JSON_INTERNAL_H */

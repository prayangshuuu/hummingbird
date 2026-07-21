/* hummingbird.h — Stable public C ABI for libhummingbird.
 *
 * This is the ONLY header external embedders should include:
 *
 *     #include <hummingbird/hummingbird.h>
 *
 * Stability contract (see docs/architecture/04-public-api.md, ADR DD-014):
 *   - Every symbol here is prefixed `hb_` and is semver-stable. It will not be
 *     removed or changed incompatibly without a major-version bump and an ADR.
 *   - Experimental, opt-in API lives in <hummingbird/hummingbird_experimental.h>
 *     under the `hb_x_` prefix and carries no stability guarantee.
 *   - Internal engine symbols use the `hbi_` prefix and are never installed.
 *
 * Status: PHASE 4 bootstrap. Only version/status introspection is implemented;
 * model loading, contexts, and decoding are declared in later milestones.
 */
#ifndef HUMMINGBIRD_H
#define HUMMINGBIRD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Semantic version of the library / ABI ─────────────────────────────── */
#define HB_VERSION_MAJOR 0
#define HB_VERSION_MINOR 0
#define HB_VERSION_PATCH 0

/* Packed integer form: MAJOR*10000 + MINOR*100 + PATCH. */
#define HB_VERSION (HB_VERSION_MAJOR * 10000 + HB_VERSION_MINOR * 100 + HB_VERSION_PATCH)

/* ── Status codes ──────────────────────────────────────────────────────────
 * Stable numeric contract: values never change once assigned (append only).
 * Mirrors the internal `hb_status` (src/common/common.h); the two must agree.
 */
typedef enum hb_status {
    HB_OK = 0,                 /* success */
    HB_ERR_UNKNOWN = 1,        /* unclassified failure */
    HB_ERR_INVALID_ARG = 2,    /* caller passed an invalid argument */
    HB_ERR_NO_MEMORY = 3,      /* allocation failed */
    HB_ERR_IO = 4,             /* filesystem / device I/O error */
    HB_ERR_NOT_FOUND = 5,      /* requested entity does not exist */
    HB_ERR_UNSUPPORTED = 6,    /* operation not supported in this build/config */
    HB_ERR_CORRUPT = 7,        /* malformed data (bad container, bounds check) */
    HB_ERR_NOT_IMPLEMENTED = 8 /* declared but not yet built (scaffold) */
} hb_status;

/* ── Version / diagnostics ──────────────────────────────────────────────── */

/* Runtime library version (may differ from the compile-time HB_VERSION the
 * caller built against). Never NULL; e.g. "0.0.0". */
const char *hb_version_string(void);

/* Packed runtime version integer (see HB_VERSION). */
int hb_version(void);

/* Human-readable, non-localized description of a status code. Never NULL. */
const char *hb_status_string(hb_status status);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HUMMINGBIRD_H */

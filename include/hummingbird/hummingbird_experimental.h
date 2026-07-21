/* hummingbird_experimental.h — opt-in, UNSTABLE public API.
 *
 * Symbols here are prefixed `hb_x_` and may change or be removed in any release
 * without a major-version bump (see ADR DD-014). Include only if you accept
 * that. Promotion to the stable surface (<hummingbird/hummingbird.h>) happens
 * via an ADR once a feature has proven itself.
 *
 * Status: PHASE 4 bootstrap — intentionally minimal so the header exists,
 * installs, and compiles.
 */
#ifndef HUMMINGBIRD_EXPERIMENTAL_H
#define HUMMINGBIRD_EXPERIMENTAL_H

#include <hummingbird/hummingbird.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 if this build includes any experimental features, else 0. */
int hb_x_available(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HUMMINGBIRD_EXPERIMENTAL_H */

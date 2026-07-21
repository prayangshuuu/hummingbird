/* main.c — the Hummingbird server frontend (scaffold).
 *
 * PHASE 4: prints intent and exits. The real server will expose an
 * OpenAI-compatible HTTP API over libhummingbird. Security note: when built
 * out, the listener must default to loopback and require explicit opt-in to
 * bind a public interface — an unauthenticated inference endpoint is a footgun.
 */
#include <hummingbird/hummingbird.h>

#include <stdio.h>

int main(void) {
    fprintf(stderr,
            "hb-server %s: not implemented yet (scaffold).\n"
            "The OpenAI-compatible gateway arrives in a later milestone (see roadmap).\n",
            hb_version_string());
    return (int)HB_ERR_NOT_IMPLEMENTED;
}

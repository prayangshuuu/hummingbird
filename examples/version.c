/* version.c — the smallest possible libhummingbird embedder.
 *
 * Proves that an external program can include only the public header, link only
 * the public library, and get useful behavior. As the ABI grows (load model,
 * create context, decode) this example grows with it.
 */
#include <hummingbird/hummingbird.h>

#include <stdio.h>

int main(void) {
    printf("libhummingbird %s (ABI %d)\n", hb_version_string(), hb_version());
    printf("HB_OK reads as: %s\n", hb_status_string(HB_OK));
    return 0;
}

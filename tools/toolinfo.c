/* toolinfo.c — placeholder for the Hummingbird offline tool suite.
 *
 * The real tools (convert → .hbm, oracle generator, quant ablation) land in
 * later milestones (DD-013, PROJECT_CONTEXT §6). This stub links the public
 * library and reports the version so the tools/ tree builds and tests green.
 */
#include <hummingbird/hummingbird.h>

#include <stdio.h>

int main(void) {
    printf("hb-tools (scaffold) — libhummingbird %s\n", hb_version_string());
    printf("planned: convert, oracle, ablation (see PROJECT_CONTEXT.md roadmap)\n");
    return 0;
}

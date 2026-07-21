/* bench_noop.c — placeholder benchmark.
 *
 * Real benchmarks (throughput, TTFT, cache hit-rate, disk bandwidth) arrive with
 * the engine and populate PROJECT_CONTEXT §9. This stub exists so the benchmarks/
 * tree compiles when -DHB_BUILD_BENCHMARKS=ON.
 */
#include <hummingbird/hummingbird.h>

#include <stdio.h>

int main(void) {
    printf("hb bench (scaffold) — nothing to measure yet (libhummingbird %s)\n",
           hb_version_string());
    return 0;
}

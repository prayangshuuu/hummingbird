#include "common/common.h"
#include "stream/stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define BENCH_ITERATIONS 100000

static double get_time(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void bench_sequential_streaming(void) {
    size_t capacities[HB_TIER_COUNT] = {1024 * 1024, 1024 * 1024, 8192 * 1024, 0};
    hbi_stream_engine_t *engine =
        hbi_stream_engine_create(capacities, HB_CACHE_POLICY_LRU, 1024 * 1024);
    hbi_scheduler_t *sched = hbi_scheduler_create(engine);

    double start = get_time();
    for (int i = 0; i < BENCH_ITERATIONS; i++) {
        hbi_scheduler_load(sched, (hbi_block_id_t)(i % 1024), (size_t)1024);
    }
    double end = get_time();

    printf("Sequential streaming (load): %f ops/sec\n", BENCH_ITERATIONS / (end - start));

    hbi_scheduler_destroy(sched);
    hbi_stream_engine_destroy(engine);
}

static void bench_random_streaming(void) {
    size_t capacities[HB_TIER_COUNT] = {1024 * 1024, 1024 * 1024, 8192 * 1024, 0};
    hbi_stream_engine_t *engine =
        hbi_stream_engine_create(capacities, HB_CACHE_POLICY_LRU, 1024 * 1024);
    hbi_scheduler_t *sched = hbi_scheduler_create(engine);

    double start = get_time();
    for (int i = 0; i < BENCH_ITERATIONS; i++) {
        hbi_scheduler_load(sched, (hbi_block_id_t)(rand() % 2048), (size_t)1024);
    }
    double end = get_time();

    printf("Random streaming (load): %f ops/sec\n", BENCH_ITERATIONS / (end - start));

    hbi_scheduler_destroy(sched);
    hbi_stream_engine_destroy(engine);
}

int main(void) {
    printf("=== Streaming Engine Benchmarks ===\n");
    srand(42u);
    bench_sequential_streaming();
    bench_random_streaming();
    return 0;
}

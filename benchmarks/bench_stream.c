#include "stream/stream.h"
#include "common/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define BENCH_ITERATIONS 100000

static double get_time() {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void bench_sequential_streaming() {
    size_t capacities[HB_TIER_COUNT] = { 1024*1024, 1024*1024, 8192*1024, 0 };
    hbi_memory_manager_t* mm = hbi_memory_manager_create(capacities);
    hbi_cache_manager_t* cache = hbi_cache_manager_create(HB_CACHE_POLICY_LRU, 1024*1024);
    hbi_scheduler_t* sched = hbi_scheduler_create(mm, cache);
    
    double start = get_time();
    for (int i = 0; i < BENCH_ITERATIONS; i++) {
        hbi_scheduler_load(sched, i % 1024, 1024);
    }
    double end = get_time();
    
    printf("Sequential streaming (load): %f ops/sec\n", BENCH_ITERATIONS / (end - start));
    
    hbi_scheduler_destroy(sched);
    hbi_cache_manager_destroy(cache);
    hbi_memory_manager_destroy(mm);
}

static void bench_random_streaming() {
    size_t capacities[HB_TIER_COUNT] = { 1024*1024, 1024*1024, 8192*1024, 0 };
    hbi_memory_manager_t* mm = hbi_memory_manager_create(capacities);
    hbi_cache_manager_t* cache = hbi_cache_manager_create(HB_CACHE_POLICY_LRU, 1024*1024);
    hbi_scheduler_t* sched = hbi_scheduler_create(mm, cache);
    
    double start = get_time();
    for (int i = 0; i < BENCH_ITERATIONS; i++) {
        hbi_scheduler_load(sched, rand() % 2048, 1024);
    }
    double end = get_time();
    
    printf("Random streaming (load): %f ops/sec\n", BENCH_ITERATIONS / (end - start));
    
    hbi_scheduler_destroy(sched);
    hbi_cache_manager_destroy(cache);
    hbi_memory_manager_destroy(mm);
}

int main() {
    printf("=== Streaming Engine Benchmarks ===\n");
    bench_sequential_streaming();
    bench_random_streaming();
    return 0;
}

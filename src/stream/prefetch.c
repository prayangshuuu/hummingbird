#include "stream/stream.h"
#include <stdlib.h>

struct hbi_prefetch_engine_t {
    hbi_scheduler_t* sched;
};

hbi_prefetch_engine_t* hbi_prefetch_engine_create(hbi_scheduler_t* sched) {
    hbi_prefetch_engine_t* prefetch = calloc(1, sizeof(hbi_prefetch_engine_t));
    if (!prefetch) return NULL;
    prefetch->sched = sched;
    return prefetch;
}

void hbi_prefetch_engine_destroy(hbi_prefetch_engine_t* prefetch) {
    if (prefetch) free(prefetch);
}

void hbi_prefetch_engine_submit(hbi_prefetch_engine_t* prefetch, hbi_block_id_t* sequence, size_t count, size_t size_per_block) {
    if (!prefetch || !prefetch->sched) return;
    
    for (size_t i = 0; i < count; i++) {
        hbi_scheduler_prefetch(prefetch->sched, sequence[i], size_per_block);
    }
}

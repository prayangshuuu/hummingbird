#include "stream/stream.h"
#include <stdlib.h>

struct hbi_locality_tracker_t {
    hbi_locality_stats_t stats;
};

hbi_locality_tracker_t* hbi_locality_tracker_create(void) {
    hbi_locality_tracker_t* tracker = calloc(1, sizeof(hbi_locality_tracker_t));
    if (!tracker) return NULL;
    return tracker;
}

void hbi_locality_tracker_destroy(hbi_locality_tracker_t* tracker) {
    if (tracker) free(tracker);
}

void hbi_locality_tracker_record_hit(hbi_locality_tracker_t* tracker, hbi_block_id_t id) {
    if (tracker) tracker->stats.hits++;
}

void hbi_locality_tracker_record_miss(hbi_locality_tracker_t* tracker, hbi_block_id_t id) {
    if (tracker) tracker->stats.misses++;
}

void hbi_locality_tracker_record_transfer(hbi_locality_tracker_t* tracker, hbi_block_id_t id, size_t bytes) {
    if (tracker) tracker->stats.bytes_transferred += bytes;
}

void hbi_locality_tracker_record_promotion(hbi_locality_tracker_t* tracker, hbi_block_id_t id) {
    if (tracker) tracker->stats.promotions++;
}

void hbi_locality_tracker_record_demotion(hbi_locality_tracker_t* tracker, hbi_block_id_t id) {
    if (tracker) tracker->stats.demotions++;
}

hbi_locality_stats_t hbi_locality_tracker_get_stats(hbi_locality_tracker_t* tracker) {
    hbi_locality_stats_t empty = {0};
    if (!tracker) return empty;
    
    // Calculate temporal locality score simply as hit rate for this milestone
    uint64_t total = tracker->stats.hits + tracker->stats.misses;
    if (total > 0) {
        tracker->stats.temporal_locality_score = (float)tracker->stats.hits / total;
    } else {
        tracker->stats.temporal_locality_score = 0.0f;
    }
    
    return tracker->stats;
}

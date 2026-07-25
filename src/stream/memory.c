#include "stream/stream.h"
#include <stdlib.h>
#include <string.h>

#define MAX_BLOCKS 1024

typedef struct {
    hbi_block_id_t id;
    hbi_memory_tier_t tier;
    size_t size;
    hbi_residency_status_t status;
    bool allocated;
} block_entry_t;

struct hbi_memory_manager_t {
    size_t tier_capacities[HB_TIER_COUNT];
    size_t tier_used[HB_TIER_COUNT];
    block_entry_t blocks[MAX_BLOCKS];
};

hbi_memory_manager_t *hbi_memory_manager_create(size_t tier_capacities[HB_TIER_COUNT]) {
    hbi_memory_manager_t *mm = calloc(1, sizeof(hbi_memory_manager_t));
    if (!mm)
        return NULL;
    for (int i = 0; i < HB_TIER_COUNT; i++) {
        mm->tier_capacities[i] = tier_capacities[i];
        mm->tier_used[i] = 0;
    }
    return mm;
}

void hbi_memory_manager_destroy(hbi_memory_manager_t *mm) {
    if (mm)
        free(mm);
}

static int find_block(hbi_memory_manager_t *mm, hbi_block_id_t id) {
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (mm->blocks[i].allocated && mm->blocks[i].id == id) {
            return i;
        }
    }
    return -1;
}

static int find_free_slot(hbi_memory_manager_t *mm) {
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (!mm->blocks[i].allocated)
            return i;
    }
    return -1;
}

bool hbi_memory_manager_allocate(hbi_memory_manager_t *mm, hbi_block_id_t id, size_t size,
                                 hbi_memory_tier_t tier) {
    if (!mm)
        return false;
    if (find_block(mm, id) >= 0)
        return false; // Already allocated
    if (mm->tier_used[tier] + size > mm->tier_capacities[tier])
        return false; // OOM

    int slot = find_free_slot(mm);
    if (slot < 0)
        return false;

    mm->blocks[slot].id = id;
    mm->blocks[slot].tier = tier;
    mm->blocks[slot].size = size;
    mm->blocks[slot].status = HB_RESIDENCY_RESIDENT;
    mm->blocks[slot].allocated = true;

    mm->tier_used[tier] += size;
    return true;
}

bool hbi_memory_manager_free(hbi_memory_manager_t *mm, hbi_block_id_t id) {
    if (!mm)
        return false;
    int slot = find_block(mm, id);
    if (slot < 0)
        return false;

    mm->tier_used[mm->blocks[slot].tier] -= mm->blocks[slot].size;
    mm->blocks[slot].allocated = false;
    return true;
}

hbi_residency_status_t hbi_memory_manager_get_residency(hbi_memory_manager_t *mm,
                                                        hbi_block_id_t id) {
    if (!mm)
        return HB_RESIDENCY_EVICTED;
    int slot = find_block(mm, id);
    if (slot < 0)
        return HB_RESIDENCY_EVICTED;
    return mm->blocks[slot].status;
}

bool hbi_memory_manager_move(hbi_memory_manager_t *mm, hbi_block_id_t id,
                             hbi_memory_tier_t new_tier) {
    if (!mm)
        return false;
    int slot = find_block(mm, id);
    if (slot < 0)
        return false;

    size_t size = mm->blocks[slot].size;
    hbi_memory_tier_t old_tier = mm->blocks[slot].tier;

    if (old_tier == new_tier)
        return true;

    if (mm->tier_used[new_tier] + size > mm->tier_capacities[new_tier]) {
        return false; // Target tier full
    }

    mm->tier_used[old_tier] -= size;
    mm->tier_used[new_tier] += size;
    mm->blocks[slot].tier = new_tier;
    return true;
}

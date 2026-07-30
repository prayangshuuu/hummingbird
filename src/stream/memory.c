#include "platform/platform.h"
#include "stream/stream.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 4096

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
    hbi_mutex *mutex;
    block_entry_t *blocks;
    size_t capacity;
    size_t count;
};

// Simple hash function for block_id
static size_t hash_id(hbi_block_id_t id) {
    size_t h = (size_t)id;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

hbi_memory_manager_t *hbi_memory_manager_create(size_t tier_capacities[HB_TIER_COUNT]) {
    hbi_memory_manager_t *mm = calloc(1, sizeof(hbi_memory_manager_t));
    if (!mm)
        return NULL;

    if (hbi_mutex_init(&mm->mutex) != HBI_OK) {
        free(mm);
        return NULL;
    }

    for (int i = 0; i < HB_TIER_COUNT; i++) {
        mm->tier_capacities[i] = tier_capacities[i];
        mm->tier_used[i] = 0;
    }

    mm->capacity = INITIAL_CAPACITY;
    mm->count = 0;
    mm->blocks = calloc(mm->capacity, sizeof(block_entry_t));
    if (!mm->blocks) {
        hbi_mutex_destroy(mm->mutex);
        free(mm);
        return NULL;
    }

    return mm;
}

void hbi_memory_manager_destroy(hbi_memory_manager_t *mm) {
    if (!mm)
        return;
    hbi_mutex_destroy(mm->mutex);
    free(mm->blocks);
    free(mm);
}

// Assumes mutex is held
static int find_block_slot(hbi_memory_manager_t *mm, hbi_block_id_t id) {
    size_t mask = mm->capacity - 1; // Assuming capacity is power of 2
    size_t idx = hash_id(id) & mask;

    // Open addressing, linear probing
    for (size_t i = 0; i < mm->capacity; i++) {
        if (!mm->blocks[idx].allocated) {
            return -1; // Not found
        }
        if (mm->blocks[idx].id == id) {
            return (int)idx;
        }
        idx = (idx + 1) & mask;
    }
    return -1;
}

// Assumes mutex is held
static bool resize_table(hbi_memory_manager_t *mm) {
    size_t new_cap = mm->capacity * 2;
    block_entry_t *new_blocks = calloc(new_cap, sizeof(block_entry_t));
    if (!new_blocks)
        return false;

    size_t mask = new_cap - 1;
    for (size_t i = 0; i < mm->capacity; i++) {
        if (mm->blocks[i].allocated) {
            size_t idx = hash_id(mm->blocks[i].id) & mask;
            while (new_blocks[idx].allocated) {
                idx = (idx + 1) & mask;
            }
            new_blocks[idx] = mm->blocks[i];
        }
    }

    free(mm->blocks);
    mm->blocks = new_blocks;
    mm->capacity = new_cap;
    return true;
}

bool hbi_memory_manager_allocate(hbi_memory_manager_t *mm, hbi_block_id_t id, size_t size,
                                 hbi_memory_tier_t tier) {
    if (!mm)
        return false;
    hbi_mutex_lock(mm->mutex);

    if (find_block_slot(mm, id) >= 0) {
        hbi_mutex_unlock(mm->mutex);
        return false; // Already allocated
    }
    if (mm->tier_used[tier] + size > mm->tier_capacities[tier]) {
        hbi_mutex_unlock(mm->mutex);
        return false; // OOM
    }

    if (mm->count >= mm->capacity / 2) {
        if (!resize_table(mm)) {
            hbi_mutex_unlock(mm->mutex);
            return false;
        }
    }

    size_t mask = mm->capacity - 1;
    size_t idx = hash_id(id) & mask;
    while (mm->blocks[idx].allocated) {
        idx = (idx + 1) & mask;
    }

    mm->blocks[idx].id = id;
    mm->blocks[idx].tier = tier;
    mm->blocks[idx].size = size;
    mm->blocks[idx].status = HB_RESIDENCY_RESIDENT;
    mm->blocks[idx].allocated = true;
    mm->count++;
    mm->tier_used[tier] += size;

    hbi_mutex_unlock(mm->mutex);
    return true;
}

bool hbi_memory_manager_free(hbi_memory_manager_t *mm, hbi_block_id_t id) {
    if (!mm)
        return false;
    hbi_mutex_lock(mm->mutex);

    int slot = find_block_slot(mm, id);
    if (slot < 0) {
        hbi_mutex_unlock(mm->mutex);
        return false;
    }

    mm->tier_used[mm->blocks[slot].tier] -= mm->blocks[slot].size;
    mm->blocks[slot].allocated = false;
    mm->count--;

    // Need to re-hash subsequent elements in the cluster to prevent breaking the chain
    size_t mask = mm->capacity - 1;
    size_t i = ((size_t)slot + 1) & mask;
    while (mm->blocks[i].allocated) {
        block_entry_t entry = mm->blocks[i];
        mm->blocks[i].allocated = false;
        mm->count--;

        size_t new_idx = hash_id(entry.id) & mask;
        while (mm->blocks[new_idx].allocated) {
            new_idx = (new_idx + 1) & mask;
        }
        mm->blocks[new_idx] = entry;
        mm->count++;

        i = (i + 1) & mask;
    }

    hbi_mutex_unlock(mm->mutex);
    return true;
}

hbi_residency_status_t hbi_memory_manager_get_residency(hbi_memory_manager_t *mm,
                                                        hbi_block_id_t id) {
    if (!mm)
        return HB_RESIDENCY_EVICTED;
    hbi_mutex_lock(mm->mutex);

    int slot = find_block_slot(mm, id);
    hbi_residency_status_t status = HB_RESIDENCY_EVICTED;
    if (slot >= 0) {
        status = mm->blocks[slot].status;
    }

    hbi_mutex_unlock(mm->mutex);
    return status;
}

bool hbi_memory_manager_move(hbi_memory_manager_t *mm, hbi_block_id_t id,
                             hbi_memory_tier_t new_tier) {
    if (!mm)
        return false;
    hbi_mutex_lock(mm->mutex);

    int slot = find_block_slot(mm, id);
    if (slot < 0) {
        hbi_mutex_unlock(mm->mutex);
        return false;
    }

    size_t size = mm->blocks[slot].size;
    hbi_memory_tier_t old_tier = mm->blocks[slot].tier;

    if (old_tier == new_tier) {
        hbi_mutex_unlock(mm->mutex);
        return true;
    }

    if (mm->tier_used[new_tier] + size > mm->tier_capacities[new_tier]) {
        hbi_mutex_unlock(mm->mutex);
        return false; // Target tier full
    }

    mm->tier_used[old_tier] -= size;
    mm->tier_used[new_tier] += size;
    mm->blocks[slot].tier = new_tier;

    hbi_mutex_unlock(mm->mutex);
    return true;
}

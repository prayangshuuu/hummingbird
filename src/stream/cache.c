#include "platform/platform.h"
#include "stream/stream.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 4096

typedef struct lru_node {
    hbi_block_id_t id;
    struct lru_node *prev;
    struct lru_node *next;
} lru_node_t;

typedef struct hash_entry {
    hbi_block_id_t id;
    lru_node_t *node;
    bool occupied;
} hash_entry_t;

struct hbi_cache_manager_t {
    hbi_cache_policy_t policy;
    size_t capacity;
    size_t count;
    lru_node_t *head;
    lru_node_t *tail;

    hbi_mutex *mutex;

    hash_entry_t *table;
    size_t table_capacity;
};

static size_t hash_id(hbi_block_id_t id) {
    size_t h = (size_t)id;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

hbi_cache_manager_t *hbi_cache_manager_create(hbi_cache_policy_t policy, size_t capacity) {
    hbi_cache_manager_t *cache = calloc(1, sizeof(hbi_cache_manager_t));
    if (!cache)
        return NULL;

    if (hbi_mutex_init(&cache->mutex) != HBI_OK) {
        free(cache);
        return NULL;
    }

    cache->policy = policy;
    cache->capacity = capacity;
    cache->table_capacity = INITIAL_CAPACITY;
    while (cache->table_capacity < capacity * 2) {
        cache->table_capacity *= 2;
    }

    cache->table = calloc(cache->table_capacity, sizeof(hash_entry_t));
    if (!cache->table) {
        hbi_mutex_destroy(cache->mutex);
        free(cache);
        return NULL;
    }

    return cache;
}

void hbi_cache_manager_destroy(hbi_cache_manager_t *cache) {
    if (!cache)
        return;

    lru_node_t *curr = cache->head;
    while (curr) {
        lru_node_t *next = curr->next;
        free(curr);
        curr = next;
    }

    hbi_mutex_destroy(cache->mutex);
    free(cache->table);
    free(cache);
}

// Assumes mutex is held
static int find_hash_slot(hbi_cache_manager_t *cache, hbi_block_id_t id) {
    size_t mask = cache->table_capacity - 1;
    size_t idx = hash_id(id) & mask;

    for (size_t i = 0; i < cache->table_capacity; i++) {
        if (!cache->table[idx].occupied) {
            return -1;
        }
        if (cache->table[idx].id == id) {
            return (int)idx;
        }
        idx = (idx + 1) & mask;
    }
    return -1;
}

// Assumes mutex is held
static bool resize_table(hbi_cache_manager_t *cache) {
    size_t new_cap = cache->table_capacity * 2;
    hash_entry_t *new_table = calloc(new_cap, sizeof(hash_entry_t));
    if (!new_table)
        return false;

    size_t mask = new_cap - 1;
    for (size_t i = 0; i < cache->table_capacity; i++) {
        if (cache->table[i].occupied) {
            size_t idx = hash_id(cache->table[i].id) & mask;
            while (new_table[idx].occupied) {
                idx = (idx + 1) & mask;
            }
            new_table[idx] = cache->table[i];
        }
    }

    free(cache->table);
    cache->table = new_table;
    cache->table_capacity = new_cap;
    return true;
}

// Assumes mutex is held
static void move_to_front(hbi_cache_manager_t *cache, lru_node_t *node) {
    if (cache->head == node)
        return;
    if (node->prev)
        node->prev->next = node->next;
    if (node->next)
        node->next->prev = node->prev;
    if (cache->tail == node)
        cache->tail = node->prev;

    node->next = cache->head;
    node->prev = NULL;
    if (cache->head)
        cache->head->prev = node;
    cache->head = node;
    if (!cache->tail)
        cache->tail = node;
}

bool hbi_cache_manager_access(hbi_cache_manager_t *cache, hbi_block_id_t id) {
    if (!cache)
        return false;
    hbi_mutex_lock(cache->mutex);

    int slot = find_hash_slot(cache, id);
    if (slot >= 0) {
        move_to_front(cache, cache->table[slot].node);
        hbi_mutex_unlock(cache->mutex);
        return true;
    }

    hbi_mutex_unlock(cache->mutex);
    return false;
}

void hbi_cache_manager_insert(hbi_cache_manager_t *cache, hbi_block_id_t id) {
    if (!cache)
        return;
    hbi_mutex_lock(cache->mutex);

    if (find_hash_slot(cache, id) >= 0) {
        hbi_mutex_unlock(cache->mutex);
        return;
    }

    if (cache->count >= cache->table_capacity / 2) {
        if (!resize_table(cache)) {
            hbi_mutex_unlock(cache->mutex);
            return;
        }
    }

    lru_node_t *node = malloc(sizeof(lru_node_t));
    if (!node) {
        hbi_mutex_unlock(cache->mutex);
        return;
    }
    node->id = id;
    node->next = NULL;
    node->prev = NULL;

    move_to_front(cache, node);
    cache->count++;

    size_t mask = cache->table_capacity - 1;
    size_t idx = hash_id(id) & mask;
    while (cache->table[idx].occupied) {
        idx = (idx + 1) & mask;
    }

    cache->table[idx].id = id;
    cache->table[idx].node = node;
    cache->table[idx].occupied = true;

    hbi_mutex_unlock(cache->mutex);
}

bool hbi_cache_manager_evict(hbi_cache_manager_t *cache, hbi_block_id_t *evicted_id) {
    if (!cache)
        return false;
    hbi_mutex_lock(cache->mutex);

    if (cache->count == 0) {
        hbi_mutex_unlock(cache->mutex);
        return false;
    }

    lru_node_t *node = cache->tail;
    if (!node) {
        hbi_mutex_unlock(cache->mutex);
        return false;
    }

    hbi_block_id_t id = node->id;
    if (evicted_id)
        *evicted_id = id;

    if (node->prev)
        node->prev->next = NULL;
    cache->tail = node->prev;
    if (cache->head == node)
        cache->head = NULL;

    // Remove from hash table
    int slot = find_hash_slot(cache, id);
    if (slot >= 0) {
        cache->table[slot].occupied = false;

        // Rehash chain
        size_t mask = cache->table_capacity - 1;
        size_t i = ((size_t)slot + 1) & mask;
        while (cache->table[i].occupied) {
            hash_entry_t entry = cache->table[i];
            cache->table[i].occupied = false;

            size_t new_idx = hash_id(entry.id) & mask;
            while (cache->table[new_idx].occupied) {
                new_idx = (new_idx + 1) & mask;
            }
            cache->table[new_idx] = entry;
            i = (i + 1) & mask;
        }
    }

    free(node);
    cache->count--;
    hbi_mutex_unlock(cache->mutex);
    return true;
}

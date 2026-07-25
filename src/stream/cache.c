#include "stream/stream.h"
#include <stdlib.h>
#include <string.h>

typedef struct lru_node {
    hbi_block_id_t id;
    struct lru_node* prev;
    struct lru_node* next;
} lru_node_t;

struct hbi_cache_manager_t {
    hbi_cache_policy_t policy;
    size_t capacity;
    size_t count;
    lru_node_t* head;
    lru_node_t* tail;
};

hbi_cache_manager_t* hbi_cache_manager_create(hbi_cache_policy_t policy, size_t capacity) {
    hbi_cache_manager_t* cache = calloc(1, sizeof(hbi_cache_manager_t));
    if (!cache) return NULL;
    cache->policy = policy;
    cache->capacity = capacity;
    return cache;
}

void hbi_cache_manager_destroy(hbi_cache_manager_t* cache) {
    if (!cache) return;
    lru_node_t* curr = cache->head;
    while (curr) {
        lru_node_t* next = curr->next;
        free(curr);
        curr = next;
    }
    free(cache);
}

static lru_node_t* find_node(hbi_cache_manager_t* cache, hbi_block_id_t id) {
    lru_node_t* curr = cache->head;
    while (curr) {
        if (curr->id == id) return curr;
        curr = curr->next;
    }
    return NULL;
}

static void move_to_front(hbi_cache_manager_t* cache, lru_node_t* node) {
    if (cache->head == node) return;
    if (node->prev) node->prev->next = node->next;
    if (node->next) node->next->prev = node->prev;
    if (cache->tail == node) cache->tail = node->prev;
    
    node->next = cache->head;
    node->prev = NULL;
    if (cache->head) cache->head->prev = node;
    cache->head = node;
    if (!cache->tail) cache->tail = node;
}

bool hbi_cache_manager_access(hbi_cache_manager_t* cache, hbi_block_id_t id) {
    if (!cache) return false;
    lru_node_t* node = find_node(cache, id);
    if (node) {
        move_to_front(cache, node);
        return true;
    }
    return false;
}

void hbi_cache_manager_insert(hbi_cache_manager_t* cache, hbi_block_id_t id) {
    if (!cache) return;
    if (find_node(cache, id)) return;
    
    lru_node_t* node = malloc(sizeof(lru_node_t));
    node->id = id;
    node->next = NULL;
    node->prev = NULL;
    
    move_to_front(cache, node);
    cache->count++;
}

bool hbi_cache_manager_evict(hbi_cache_manager_t* cache, hbi_block_id_t* evicted_id) {
    if (!cache || cache->count == 0) return false;
    
    lru_node_t* node = cache->tail;
    if (!node) return false;
    
    if (evicted_id) *evicted_id = node->id;
    
    if (node->prev) node->prev->next = NULL;
    cache->tail = node->prev;
    if (cache->head == node) cache->head = NULL;
    
    free(node);
    cache->count--;
    return true;
}
